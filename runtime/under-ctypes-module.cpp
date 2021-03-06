// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <ffi.h>

#include "cpython-data.h"

#include "builtins.h"
#include "bytes-builtins.h"
#include "frame.h"
#include "globals.h"
#include "module-builtins.h"
#include "modules.h"
#include "objects.h"
#include "os.h"
#include "thread.h"
#include "type-builtins.h"

namespace py {

// a table entry describing a predefined ctypes type
struct FieldDesc {
  char code;
  ffi_type* pffi_type;
};

static_assert(sizeof(long long) == 8, "expected sizof(long long) == 8");
static_assert(sizeof(wchar_t) == 4, "expected sizeof(wchar) == 4");
static_assert(sizeof(bool) == 1, "expected sizeof(bool) == 1");

// clang-format off
static FieldDesc format_table[] = {
    {'s', &ffi_type_pointer},
    {'b', &ffi_type_schar},
    {'B', &ffi_type_uchar},
    {'c', &ffi_type_schar},
    {'d', &ffi_type_double},
    {'g', &ffi_type_longdouble},
    {'f', &ffi_type_float},
    {'h', &ffi_type_sshort},
    {'H', &ffi_type_ushort},
    {'i', &ffi_type_sint},
    {'I', &ffi_type_uint},
    {'l', &ffi_type_slong},
    {'L', &ffi_type_ulong},
    {'q', &ffi_type_sint64},  // 'q' and 'Q' are `long long`
    {'Q', &ffi_type_uint64},
    {'P', &ffi_type_pointer},
    {'z', &ffi_type_pointer},
    {'u', &ffi_type_sint32},  // 'u' and 'U' are `wchar_t`
    {'U', &ffi_type_pointer},
    {'Z', &ffi_type_pointer},
    {'?', &ffi_type_uint8},  // '?' is `bool`
    {'O', &ffi_type_pointer},
    {0, nullptr},
};
// clang-format on

static FieldDesc* fieldDesc(const char fmt) {
  for (FieldDesc* entry = format_table; entry->code != 0; ++entry) {
    if (entry->code == fmt) return entry;
  }
  return nullptr;
}

static PyObject* cast(void* /* ptr */, PyObject* /* src */,
                      PyObject* /* ctype */) {
  UNIMPLEMENTED("_ctypes cast");
}

static PyObject* stringAt(const char* /* ptr */, int /* size */) {
  UNIMPLEMENTED("_ctypes stringAt");
}

static PyObject* wstringAt(const wchar_t* /* ptr */, int /* size */) {
  UNIMPLEMENTED("_ctypes wstringAt");
}

void FUNC(_ctypes, __init_module__)(Thread* thread, const Module& module,
                                    View<byte> bytecode) {
  executeFrozenModule(thread, module, bytecode);

  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();

  Object rtld_local_name(&scope, runtime->symbols()->at(ID(RTLD_LOCAL)));
  Object rtld_local(&scope, runtime->newInt(OS::kRtldLocal));
  Object rtld_global_name(&scope, runtime->symbols()->at(ID(RTLD_GLOBAL)));
  Object rtld_global(&scope, runtime->newInt(OS::kRtldGlobal));
  moduleAtPut(thread, module, rtld_local_name, rtld_local);
  moduleAtPut(thread, module, rtld_global_name, rtld_global);

  Object cast_addr_name(&scope, runtime->symbols()->at(ID(_cast_addr)));
  Object cast_addr(&scope, runtime->newIntFromCPtr(bit_cast<void*>(&cast)));
  moduleAtPut(thread, module, cast_addr_name, cast_addr);

  Object memmove_addr_name(&scope, runtime->symbols()->at(ID(_memmove_addr)));
  Object memmove_addr(&scope,
                      runtime->newIntFromCPtr(bit_cast<void*>(&memmove)));
  moduleAtPut(thread, module, memmove_addr_name, memmove_addr);

  Object memset_addr_name(&scope, runtime->symbols()->at(ID(_memset_addr)));
  Object memset_addr(&scope, runtime->newIntFromCPtr(bit_cast<void*>(&memset)));
  moduleAtPut(thread, module, memset_addr_name, memset_addr);

  Object string_at_addr_name(&scope,
                             runtime->symbols()->at(ID(_string_at_addr)));
  Object string_at_addr(&scope,
                        runtime->newIntFromCPtr(bit_cast<void*>(&stringAt)));
  moduleAtPut(thread, module, string_at_addr_name, string_at_addr);

  Object wstring_at_addr_name(&scope,
                              runtime->symbols()->at(ID(_wstring_at_addr)));
  Object wstring_at_addr(&scope,
                         runtime->newIntFromCPtr(bit_cast<void*>(&wstringAt)));
  moduleAtPut(thread, module, wstring_at_addr_name, wstring_at_addr);
}

RawObject FUNC(_ctypes, _CharArray_value_to_bytes)(Thread* thread,
                                                   Arguments args) {
  HandleScope scope(thread);
  Object value(&scope, args.get(0));
  word length = intUnderlying(args.get(1)).asWord();
  if (value.isMmap()) {
    Pointer value_ptr(&scope, Mmap::cast(*value).data());
    DCHECK(value_ptr.length() >= length, "Mmap shorter than ctypes.Array");
    byte* cptr = reinterpret_cast<byte*>(value_ptr.cptr());
    word first_nul = Utils::memoryFindChar(cptr, length, '\0');
    return thread->runtime()->newBytesWithAll(
        {cptr, first_nul == -1 ? length : first_nul});
  }
  CHECK(value.isBytearray(), "unexpected ctypes.Array._value type");
  Bytes value_bytes(&scope, Bytearray::cast(*value).items());
  word first_nul = value_bytes.findByte(0, '\0', length);
  return bytesSubseq(thread, value_bytes, 0,
                     first_nul == -1 ? length : first_nul);
}

RawObject FUNC(_ctypes, _call_cfuncptr)(Thread* thread, Arguments args) {
  HandleScope scope(thread);

  void* addr = Int::cast(args.get(0)).asCPtr();
  Str type(&scope, args.get(1));
  switch (type.byteAt(0)) {
    case 'i': {
      int int_result = reinterpret_cast<int (*)()>(addr)();
      return thread->runtime()->newInt(int_result);
    }
    case 'z': {
      char* bytes_result = reinterpret_cast<char* (*)()>(addr)();
      return thread->runtime()->newBytesWithAll(View<byte>(
          reinterpret_cast<byte*>(bytes_result), std::strlen(bytes_result)));
    }
    default: {
      UNIMPLEMENTED("Not yet supported return value type");
    }
  }
}

RawObject FUNC(_ctypes, _memset)(Thread*, Arguments args) {
  void* addr = Int::cast(args.get(0)).asCPtr();
  OptInt<int> val = Int::cast(args.get(1)).asInt<int>();
  if (val.error != CastError::None) {
    UNIMPLEMENTED("ctypes.memset called with non-integer value");
  }
  OptInt<size_t> size = Int::cast(args.get(2)).asInt<size_t>();
  if (size.error != CastError::None) {
    UNIMPLEMENTED("ctypes.memset called with non-size_t size");
  }
  std::memset(addr, val.value, size.value);
  return args.get(0);
}

RawObject FUNC(_ctypes, _shared_object_symbol_address)(Thread* thread,
                                                       Arguments args) {
  HandleScope scope(thread);
  Int handle(&scope, args.get(0));
  Str name(&scope, args.get(1));
  unique_c_ptr<char> name_cstr(name.toCStr());
  const char* error_msg = nullptr;
  void* address = OS::sharedObjectSymbolAddress(handle.asCPtr(),
                                                name_cstr.get(), &error_msg);
  if (address == nullptr) {
    return thread->raiseWithFmt(LayoutId::kAttributeError, "%s",
                                error_msg != nullptr ? error_msg : "");
  }
  return thread->runtime()->newIntFromCPtr(address);
}

RawObject FUNC(_ctypes, _addressof)(Thread* thread, Arguments args) {
  HandleScope scope(thread);
  Object value(&scope, args.get(0));
  if (value.isMmap()) {
    Object buf(&scope, Mmap::cast(*value).data());
    Pointer pointer(&scope, *buf);
    return thread->runtime()->newIntFromCPtr(pointer.cptr());
  }
  UNIMPLEMENTED("Cannot construct pointer from non-mmap yet");
}

RawObject FUNC(_ctypes, _SimpleCData_value_to_type)(Thread* thread,
                                                    Arguments args) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Object value(&scope, args.get(0));
  Str type(&scope, args.get(1));
  Int offset(&scope, args.get(2));
  switch (type.byteAt(0)) {
    case 'H':
      if (value.isMmap()) {
        Pointer value_ptr(&scope, Mmap::cast(*value).data());
        CHECK(value_ptr.length() >= 2, "Not enough memory");
        uint16_t* cptr = reinterpret_cast<uint16_t*>(value_ptr.cptr());
        return runtime->newInt(cptr[offset.asWord()]);
      }
      if (value.isUnbound()) {
        return SmallInt::fromWord(0);
      }
      return *value;
    case 'L':
      if (value.isMmap()) {
        Pointer value_ptr(&scope, Mmap::cast(*value).data());
        CHECK(static_cast<uword>(value_ptr.length()) >= sizeof(unsigned long),
              "Not enough memory");
        long* cptr = reinterpret_cast<long*>(value_ptr.cptr());
        return runtime->newInt(cptr[offset.asWord()]);
      }
      if (value.isUnbound()) {
        return SmallInt::fromWord(0);
      }
      return *value;
    default:
      UNIMPLEMENTED("Cannot get values of non-uint16 objects yet");
  }
}

RawObject FUNC(_ctypes, dlopen)(Thread* thread, Arguments args) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();

  Object mode_obj(&scope, args.get(1));
  if (!mode_obj.isInt()) {
    return thread->raiseRequiresType(mode_obj, ID(int));
  }
  OptInt<int> mode_opt = Int::cast(*mode_obj).asInt<int>();
  if (mode_opt.error != CastError::None) {
    return thread->raiseWithFmt(LayoutId::kOverflowError,
                                "Python int too large to convert to C long");
  }
  int mode = mode_opt.value | OS::kRtldNow;
  unique_c_ptr<char> name_cstr;
  Object name_obj(&scope, args.get(0));
  if (name_obj.isNoneType()) {
    name_cstr.reset(nullptr);
  } else if (name_obj.isStr()) {
    name_cstr.reset(Str::cast(*name_obj).toCStr());
  } else {
    return thread->raiseRequiresType(name_obj, ID(str));
  }

  const char* error_msg = nullptr;
  void* handle = OS::openSharedObject(name_cstr.get(), mode, &error_msg);
  if (handle == nullptr) {
    return thread->raiseWithFmt(LayoutId::kOSError, "%s",
                                error_msg != nullptr ? error_msg : "");
  }
  return runtime->newIntFromCPtr(handle);
}

RawObject FUNC(_ctypes, _sizeof_typeclass)(Thread*, Arguments args) {
  DCHECK(args.get(0).isStr(), "bad internal call");
  FieldDesc* field_desc = fieldDesc(Str::cast(args.get(0)).byteAt(0));
  size_t size = field_desc->pffi_type->size;
  return SmallInt::fromWord(size);
}

}  // namespace py
