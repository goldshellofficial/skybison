#include <signal.h>
#include <unistd.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>

#include "cpython-data.h"
#include "cpython-func.h"

#include "capi-handles.h"
#include "exception-builtins.h"
#include "modules.h"
#include "os.h"
#include "runtime.h"

extern "C" int _PyCapsule_Init(void);
extern "C" int _PySTEntry_Init(void);

// TODO(T57880525): Reconcile these flags with sys.py
int Py_BytesWarningFlag = 0;
int Py_DebugFlag = 0;
int Py_DontWriteBytecodeFlag = 0;
int Py_FrozenFlag = 0;
int Py_HashRandomizationFlag = 0;
int Py_IgnoreEnvironmentFlag = 0;
int Py_InspectFlag = 0;
int Py_InteractiveFlag = 0;
int Py_IsolatedFlag = 0;
int Py_NoSiteFlag = 0;
int Py_NoUserSiteDirectory = 0;
int Py_OptimizeFlag = 0;
int Py_QuietFlag = 0;
int Py_UTF8Mode = 1;
int Py_UnbufferedStdioFlag = 0;
int Py_UseClassExceptionsFlag = 1;
int Py_VerboseFlag = 0;

namespace py {

PY_EXPORT PyOS_sighandler_t PyOS_getsig(int signum) {
  return OS::signalHandler(signum);
}

PY_EXPORT PyOS_sighandler_t PyOS_setsig(int signum, PyOS_sighandler_t handler) {
  return OS::setSignalHandler(signum, handler);
}

PY_EXPORT int Py_AtExit(void (*/* func */)(void)) {
  UNIMPLEMENTED("Py_AtExit");
}

PY_EXPORT void Py_EndInterpreter(PyThreadState* /* e */) {
  UNIMPLEMENTED("Py_EndInterpreter");
}

PY_EXPORT void Py_Exit(int status_code) {
  if (Py_FinalizeEx() < 0) {
    status_code = 120;
  }

  std::exit(status_code);
}

PY_EXPORT void _Py_NO_RETURN Py_FatalError(const char* msg) {
  // TODO(T39151288): Correctly print exceptions when the current thread holds
  // the GIL
  std::fprintf(stderr, "Fatal Python error: %s\n", msg);
  Thread* thread = Thread::current();
  if (thread != nullptr) {
    if (thread->hasPendingException()) {
      printPendingException(thread);
    } else {
      Utils::printTracebackToStderr();
    }
  }
  std::abort();
}

// The file descriptor fd is considered ``interactive'' if either:
//   a) isatty(fd) is TRUE, or
//   b) the -i flag was given, and the filename associated with the descriptor
//      is NULL or "<stdin>" or "???".
PY_EXPORT int Py_FdIsInteractive(FILE* fp, const char* filename) {
  if (::isatty(fileno(fp))) {
    return 1;
  }
  if (!Py_InteractiveFlag) {
    return 0;
  }
  return filename == nullptr || std::strcmp(filename, "<stdin>") == 0 ||
         std::strcmp(filename, "???") == 0;
}

PY_EXPORT void Py_Finalize() { Py_FinalizeEx(); }

PY_EXPORT int Py_FinalizeEx() {
  Thread* thread = Thread::current();
  Runtime* runtime = thread->runtime();
  delete runtime;
  return 0;
}

PY_EXPORT void Py_Initialize() { Py_InitializeEx(1); }

PY_EXPORT void Py_InitializeEx(int initsigs) {
  CHECK(initsigs == 1, "Skipping signal handler registration unimplemented");
  // TODO(T63603973): Reduce initial heap size once we can auto-grow the heap
  new Runtime(1 * kGiB);

  CHECK(_PyCapsule_Init() == 0, "Failed to initialize PyCapsule");
  CHECK(_PySTEntry_Init() == 0, "Failed to initialize PySTEntry");
  // TODO(T43142858) We should rather have the site importing in the runtime
  // constructor. Though for that we need a way to communicate the value of
  // Py_NoSiteFlag.
  if (!Py_NoSiteFlag) {
    PyObject* module = PyImport_ImportModule("site");
    if (module == nullptr) {
      py::Utils::printDebugInfoAndAbort();
    }
    Py_DECREF(module);
  }
}

PY_EXPORT int Py_IsInitialized() { UNIMPLEMENTED("Py_IsInitialized"); }

PY_EXPORT PyThreadState* Py_NewInterpreter() {
  UNIMPLEMENTED("Py_NewInterpreter");
}

PY_EXPORT wchar_t* Py_GetProgramName() { return Runtime::programName(); }

PY_EXPORT wchar_t* Py_GetPythonHome() { UNIMPLEMENTED("Py_GetPythonHome"); }

PY_EXPORT void Py_SetProgramName(wchar_t* name) {
  if (name != nullptr && name[0] != L'\0') {
    Runtime::setProgramName(name);
  }
}

PY_EXPORT void Py_SetPythonHome(wchar_t* /* home */) {
  UNIMPLEMENTED("Py_SetPythonHome");
}

struct AtExitContext {
  void (*func)(PyObject*);
  PyObject* module;
};

static void callAtExitFunction(void* context) {
  DCHECK(context != nullptr, "context must not be null");
  AtExitContext* thunk = reinterpret_cast<AtExitContext*>(context);
  DCHECK(thunk->func != nullptr, "function must not be null");
  thunk->func(thunk->module);
  // CPython does not own the reference, but that's dangerous.
  Py_DECREF(thunk->module);
  PyErr_Clear();
  delete thunk;
}

PY_EXPORT void _Py_PyAtExit(void (*func)(PyObject*), PyObject* module) {
  AtExitContext* thunk = new AtExitContext;
  thunk->func = func;
  // CPython does not own the reference, but that's dangerous.
  Py_INCREF(module);
  thunk->module = module;
  Thread::current()->runtime()->setAtExit(callAtExitFunction, thunk);
}

PY_EXPORT void _Py_RestoreSignals() {
  PyOS_setsig(SIGPIPE, SIG_DFL);
  PyOS_setsig(SIGXFSZ, SIG_DFL);
}

// NOTE: this implementation does not work for Android
PY_EXPORT char* _Py_SetLocaleFromEnv(int category) {
  return std::setlocale(category, "");
}

}  // namespace py
