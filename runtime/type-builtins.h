#pragma once

#include "frame.h"
#include "globals.h"
#include "objects.h"
#include "runtime.h"
#include "thread.h"

namespace python {

class TypeBuiltins {
 public:
  static void initialize(Runtime* runtime);

  static RawObject dunderCall(Thread* thread, Frame* caller, word nargs);
  static RawObject dunderCallKw(Thread* thread, Frame* caller, word nargs);
  static RawObject dunderNew(Thread* thread, Frame* frame, word nargs);
  static RawObject dunderInit(Thread* thread, Frame* frame, word nargs);
  static RawObject dunderRepr(Thread* thread, Frame* frame, word nargs);

 private:
  static const BuiltinMethod kMethods[];

  DISALLOW_IMPLICIT_CONSTRUCTORS(TypeBuiltins);
};

}  // namespace python
