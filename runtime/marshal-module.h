#pragma once

#include "frame.h"
#include "globals.h"
#include "objects.h"
#include "runtime.h"

namespace python {

extern word marshalVersion;

class MarshalModule : public ModuleBase<MarshalModule, SymbolId::kMarshal> {
 public:
  static void postInitialize(Thread* thread, Runtime* runtime,
                             const Module& module);
  static RawObject loads(Thread* thread, Frame* frame, word nargs);

  static const BuiltinMethod kBuiltinMethods[];
};

}  // namespace python
