#include "array-module.h"

#include "frozen-modules.h"
#include "handles.h"
#include "modules.h"
#include "objects.h"
#include "runtime.h"
#include "symbols.h"
#include "thread.h"

namespace py {

const BuiltinType ArrayModule::kBuiltinTypes[] = {
    {ID(array), LayoutId::kArray},
    {SymbolId::kSentinelId, LayoutId::kSentinelId},
};

void ArrayModule::initialize(Thread* thread, const Module& module) {
  moduleAddBuiltinTypes(thread, module, kBuiltinTypes);
  executeFrozenModule(thread, &kArrayModuleData, module);
}

const BuiltinAttribute ArrayBuiltins::kAttributes[] = {
    {ID(_typecode), Array::kTypecodeOffset, AttributeFlags::kReadOnly},
    {SymbolId::kSentinelId, -1},
};

}  // namespace py
