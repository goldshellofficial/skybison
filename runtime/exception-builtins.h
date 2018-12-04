#pragma once

#include "globals.h"
#include "runtime.h"

namespace python {

// Create an exception of the given type, which should derive from
// BaseException. If value is None, no arguments will be passed to the
// constructor, if value is a tuple, it will be unpacked as arguments, and
// otherwise it will be the single argument.
RawObject createException(Thread* thread, const Type& type,
                          const Object& value);

// Internal equivalent to PyErr_NormalizeException(): If exc is a Type subtype,
// ensure that value is an instance of it (or a subtype). If a new exception
// with a traceback is raised during normalization traceback will be set to the
// new traceback.
void normalizeException(Thread* thread, Object* exc, Object* value,
                        Object* traceback);

class BaseExceptionBuiltins {
 public:
  static void initialize(Runtime* runtime);

  static RawObject dunderInit(Thread* thread, Frame* frame, word nargs);

 private:
  static const BuiltinAttribute kAttributes[];
  static const BuiltinMethod kMethods[];

  DISALLOW_IMPLICIT_CONSTRUCTORS(BaseExceptionBuiltins);
};

class StopIterationBuiltins {
 public:
  static void initialize(Runtime* runtime);

  static RawObject dunderInit(Thread* thread, Frame* frame, word nargs);

 private:
  static const BuiltinAttribute kAttributes[];
  static const BuiltinMethod kMethods[];

  DISALLOW_IMPLICIT_CONSTRUCTORS(StopIterationBuiltins);
};

class SystemExitBuiltins {
 public:
  static void initialize(Runtime* runtime);

  static RawObject dunderInit(Thread* thread, Frame* frame, word nargs);

 private:
  static const BuiltinAttribute kAttributes[];
  static const BuiltinMethod kMethods[];

  DISALLOW_IMPLICIT_CONSTRUCTORS(SystemExitBuiltins);
};

class ImportErrorBuiltins {
 public:
  static void initialize(Runtime* runtime);

 private:
  static const BuiltinAttribute kAttributes[];

  DISALLOW_IMPLICIT_CONSTRUCTORS(ImportErrorBuiltins);
};

}  // namespace python
