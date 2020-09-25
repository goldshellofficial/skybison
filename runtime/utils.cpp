#include "utils.h"

#include <dlfcn.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include "debugging.h"
#include "file.h"
#include "frame.h"
#include "handles.h"
#include "runtime.h"
#include "thread.h"
#include "traceback-builtins.h"

namespace py {

class TracebackPrinter : public FrameVisitor {
 public:
  bool visit(Frame* frame) {
    std::stringstream line;
    if (const char* invalid_frame = frame->isInvalid()) {
      line << "  Invalid frame (" << invalid_frame << ")";
      lines_.emplace_back(line.str());
      return false;
    }

    DCHECK(!frame->isSentinel(), "should not be called for sentinel");
    Thread* thread = Thread::current();
    HandleScope scope(thread);
    Function function(&scope, frame->function());
    Object code_obj(&scope, function.code());
    if (code_obj.isCode()) {
      Code code(&scope, *code_obj);

      // Extract filename
      if (code.filename().isStr()) {
        char* filename = Str::cast(code.filename()).toCStr();
        line << "  File \"" << filename << "\", ";
        std::free(filename);
      } else {
        line << "  File \"<unknown>\",  ";
      }

      // Extract line number unless it is a native functions.
      if (!code.isNative() && code.lnotab().isBytes()) {
        // virtualPC() points to the next PC. The currently executing PC
        // should be immediately before this when raising an exception which
        // should be the only relevant case for managed code. This value will
        // be off when we produce debug output in a failed `CHECK` or in lldb
        // immediately after a jump.
        word pc = Utils::maximum(frame->virtualPC() - kCodeUnitSize, word{0});
        word linenum = code.offsetToLineNum(pc);
        line << "line " << linenum << ", ";
      }
    }

    Object name(&scope, function.name());
    if (name.isStr()) {
      unique_c_ptr<char> name_cstr(Str::cast(*name).toCStr());
      line << "in " << name_cstr.get();
    } else {
      line << "in <invalid name>";
    }

    if (code_obj.isCode()) {
      Code code(&scope, *code_obj);
      if (code.isNative()) {
        void* fptr = Int::cast(code.code()).asCPtr();
        line << "  <native function at " << fptr << " (";

        Dl_info info = Dl_info();
        if (dladdr(fptr, &info) && info.dli_sname != nullptr) {
          line << info.dli_sname;
        } else {
          line << "no symbol found";
        }
        line << ")>";
      }
    }

    lines_.emplace_back(line.str());
    return true;
  }

  void print(std::ostream* os) {
    *os << "Traceback (most recent call last):\n";
    for (auto it = lines_.rbegin(); it != lines_.rend(); it++) {
      *os << *it << '\n';
    }
    os->flush();
  }

 private:
  std::vector<std::string> lines_;
};

const byte Utils::kHexDigits[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

word Utils::memoryFind(const byte* haystack, word haystack_len,
                       const byte* needle, word needle_len) {
  DCHECK(haystack != nullptr, "haystack cannot be null");
  DCHECK(needle != nullptr, "needle cannot be null");
  DCHECK(haystack_len >= 0, "haystack length must be nonnegative");
  DCHECK(needle_len >= 0, "needle length must be nonnegative");
  // We need something to compare
  if (haystack_len == 0 || needle_len == 0) return -1;
  // The needle is too big to be contained in haystack
  if (haystack_len < needle_len) return -1;
  const void* result;
  if (needle_len == 1) {
    // Fast path: one character
    result = std::memchr(haystack, *needle, haystack_len);
  } else {
    result = ::memmem(haystack, haystack_len, needle, needle_len);
  }
  if (result == nullptr) return -1;
  return static_cast<const byte*>(result) - haystack;
}

word Utils::memoryFindChar(const byte* haystack, word haystack_len,
                           byte needle) {
  DCHECK(haystack != nullptr, "haystack cannot be null");
  DCHECK(haystack_len >= 0, "haystack length must be nonnegative");
  const void* result = std::memchr(haystack, needle, haystack_len);
  if (result == nullptr) return -1;
  return static_cast<const byte*>(result) - haystack;
}

word Utils::memoryFindCharReverse(const byte* haystack, word haystack_len,
                                  byte needle) {
  DCHECK(haystack != nullptr, "haystack cannot be null");
  DCHECK(haystack_len >= 0, "haystack length must be nonnegative");
  for (word i = haystack_len - 1; i >= 0; i--) {
    if (haystack[i] == needle) return i;
  }
  return -1;
}

word Utils::memoryFindReverse(const byte* haystack, word haystack_len,
                              const byte* needle, word needle_len) {
  DCHECK(haystack != nullptr, "haystack cannot be null");
  DCHECK(needle != nullptr, "needle cannot be null");
  DCHECK(haystack_len >= 0, "haystack length must be nonnegative");
  DCHECK(needle_len >= 0, "needle length must be nonnegative");
  // We need something to compare
  if (haystack_len == 0 || needle_len == 0) return -1;
  // The needle is too big to be contained in haystack
  if (haystack_len < needle_len) return -1;
  byte needle_start = *needle;
  if (needle_len == 1) {
    // Fast path: one character
    return memoryFindCharReverse(haystack, haystack_len, needle_start);
  }
  // The last position where its possible to find needle in haystack
  word last_offset = haystack_len - needle_len;
  for (word i = last_offset; i >= 0; i--) {
    if (haystack[i] == needle_start &&
        std::memcmp(haystack + i, needle, needle_len) == 0) {
      return i;
    }
  }
  return -1;
}

void Utils::printTracebackToStderr() { printTraceback(&std::cerr); }

void Utils::printTraceback(std::ostream* os) {
  TracebackPrinter printer;
  Thread::current()->visitFrames(&printer);
  printer.print(os);
}

void Utils::printDebugInfoAndAbort() {
  static thread_local bool aborting = false;
  if (aborting) {
    std::cerr << "Attempting to abort while already aborting. Not printing "
                 "another traceback.\n";
    std::abort();
  }
  aborting = true;

  Thread* thread = Thread::current();
  if (thread != nullptr) {
    Runtime* runtime = thread->runtime();
    runtime->printTraceback(thread, File::kStderr);
    if (thread->hasPendingException()) {
      HandleScope scope(thread);
      Object type(&scope, thread->pendingExceptionType());
      Object value(&scope, thread->pendingExceptionValue());
      Traceback traceback(&scope, thread->pendingExceptionTraceback());
      thread->clearPendingException();

      std::cerr << "Pending exception\n  Type          : " << type
                << "\n  Value         : " << value;
      if (runtime->isInstanceOfBaseException(*value)) {
        BaseException exception(&scope, *value);
        std::cerr << "\n  Exception Args: " << exception.args();
      }
      std::cerr << "\n  Traceback     : " << traceback << '\n';

      ValueCell stderr_cell(&scope, runtime->sysStderr());
      if (!stderr_cell.isUnbound()) {
        Object stderr(&scope, stderr_cell.value());
        CHECK(!tracebackWrite(thread, traceback, stderr).isErrorException(),
              "failed to print traceback");
      }
    }
  }
  std::abort();
}

}  // namespace py
