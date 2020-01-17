#pragma once

#include "globals.h"
#include "handles.h"
#include "objects.h"
#include "runtime.h"
#include "symbols.h"
#include "thread.h"

namespace py {

struct BuiltinType {
  SymbolId name;
  LayoutId type;
};

struct ModuleInitializer {
  SymbolId name;
  void (*create_module)(Thread*);
};

class ModuleBaseBase {
 public:
  static const BuiltinMethod kBuiltinMethods[];
  static const BuiltinType kBuiltinTypes[];
  static const char kFrozenData[];
};

void moduleAddBuiltinFunctions(Thread* thread, const Module& module,
                               const BuiltinMethod* functions);
void moduleAddBuiltinTypes(Thread* thread, const Module& module,
                           const BuiltinType* types);

template <typename T, SymbolId name>
class ModuleBase : public ModuleBaseBase {
 public:
  static void initialize(Thread* thread) {
    HandleScope scope(thread);
    Runtime* runtime = thread->runtime();
    Module module(&scope, runtime->createModule(thread, name));
    moduleAddBuiltinFunctions(thread, module, T::kBuiltinMethods);
    moduleAddBuiltinTypes(thread, module, T::kBuiltinTypes);
    runtime->executeFrozenModule(thread, T::kFrozenData, module);
  }
};

extern const ModuleInitializer kBuiltinModules[];

}  // namespace py
