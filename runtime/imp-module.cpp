#include "imp-module.h"

#include "frame.h"
#include "globals.h"
#include "objects.h"
#include "runtime.h"

namespace python {

RawObject builtinImpAcquireLock(Thread* /* thread */, Frame* /* frame */,
                                word /* nargs */) {
  UNIMPLEMENTED("acquire_lock");
}

RawObject builtinImpCreateBuiltin(Thread* /* thread */, Frame* /* frame */,
                                  word /* nargs */) {
  UNIMPLEMENTED("create_builtin");
}

RawObject builtinImpExecBuiltin(Thread* /* thread */, Frame* /* frame */,
                                word /* nargs */) {
  UNIMPLEMENTED("exec_builtin");
}

RawObject builtinImpExecDynamic(Thread* /* thread */, Frame* /* frame */,
                                word /* nargs */) {
  UNIMPLEMENTED("exec_dynamic");
}

RawObject builtinImpExtensionSuffixes(Thread* /* thread */, Frame* /* frame */,
                                      word /* nargs */) {
  UNIMPLEMENTED("extension_suffixes");
}

RawObject builtinImpFixCoFilename(Thread* /* thread */, Frame* /* frame */,
                                  word /* nargs */) {
  UNIMPLEMENTED("_fix_co_filename");
}

RawObject builtinImpGetFrozenObject(Thread* /* thread */, Frame* /* frame */,
                                    word /* nargs */) {
  UNIMPLEMENTED("get_frozen_object");
}

extern "C" struct _inittab _PyImport_Inittab[];

RawObject builtinImpIsBuiltin(Thread* thread, Frame* frame, word nargs) {
  if (nargs != 1) {
    return thread->raiseTypeError(thread->runtime()->newStrFromFormat(
        "expected 1 argument, got %ld", nargs - 1));
  }
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Object name_obj(&scope, args.get(0));
  if (!runtime->isInstanceOfStr(name_obj)) {
    return thread->raiseTypeErrorWithCStr("is_builtin requires a str object");
  }
  Str name(&scope, *name_obj);

  // Special case internal runtime modules
  Symbols* symbols = runtime->symbols();
  if (name.equals(symbols->Builtins()) || name.equals(symbols->UnderThread()) ||
      name.equals(symbols->Sys()) || name.equals(symbols->UnderWeakRef())) {
    return RawSmallInt::fromWord(-1);
  }

  // Iterate the list of runtime and extension builtin modules
  for (int i = 0; _PyImport_Inittab[i].name != nullptr; i++) {
    if (name.equalsCStr(_PyImport_Inittab[i].name)) {
      return RawSmallInt::fromWord(1);
    }
  }
  return RawSmallInt::fromWord(0);
}

RawObject builtinImpIsFrozen(Thread* thread, Frame* frame, word nargs) {
  if (nargs != 1) {
    return thread->raiseTypeError(thread->runtime()->newStrFromFormat(
        "expected 1 argument, got %ld", nargs - 1));
  }
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object name(&scope, args.get(0));
  if (!thread->runtime()->isInstanceOfStr(name)) {
    return thread->raiseTypeErrorWithCStr("is_frozen requires a str object");
  }
  // Always return False
  return RawBool::falseObj();
}

RawObject builtinImpIsFrozenPackage(Thread* /* thread */, Frame* /* frame */,
                                    word /* nargs */) {
  UNIMPLEMENTED("is_frozen_package");
}

RawObject builtinImpReleaseLock(Thread* /* thread */, Frame* /* frame */,
                                word /* nargs */) {
  UNIMPLEMENTED("release_lock");
}

}  // namespace python
