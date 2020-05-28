#pragma once

#include "frame.h"
#include "globals.h"
#include "modules.h"
#include "objects.h"
#include "runtime.h"

namespace py {

class UnderContextvarsModule {
 public:
  static void initialize(Thread* thread, const Module& module);

 private:
  static const BuiltinType kBuiltinTypes[];
};

void initializeUnderContextvarsTypes(Thread* thread);

}  // namespace py