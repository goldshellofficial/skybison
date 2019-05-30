#include "symbols.h"

#include "runtime.h"
#include "visitor.h"

namespace python {

// clang-format off
static const char* kPredefinedSymbols[] = {
#define DEFINE_SYMBOL(symbol, value) value,
  FOREACH_SYMBOL(DEFINE_SYMBOL)
#undef DEFINE_SYMBOL
};
// clang-format on

Symbols::Symbols(Runtime* runtime) {
  symbols_ = new RawObject[static_cast<int>(SymbolId::kMaxId)];
#define ADD_SYMBOL(symbol, value)                                              \
  symbols_[static_cast<int>(SymbolId::k##symbol)] =                            \
      runtime->newStrFromCStr(value);
  FOREACH_SYMBOL(ADD_SYMBOL)
#undef ADD_SYMBOL
}

Symbols::~Symbols() { delete[] symbols_; }

void Symbols::visit(PointerVisitor* visitor) {
  for (word i = 0; i < static_cast<int>(SymbolId::kMaxId); i++) {
    visitor->visitPointer(&symbols_[i]);
  }
}

const char* Symbols::predefinedSymbolAt(SymbolId id) {
  int index = static_cast<int>(id);
  DCHECK_INDEX(index, static_cast<int>(SymbolId::kMaxId));
  return kPredefinedSymbols[index];
}

}  // namespace python
