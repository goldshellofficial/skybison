#include "function-builtins.h"

#include "frame.h"
#include "globals.h"
#include "object-builtins.h"
#include "objects.h"
#include "runtime.h"
#include "str-builtins.h"
#include "thread.h"

namespace py {

RawObject functionFromMethodDef(Thread* thread, const char* c_name, void* meth,
                                const char* c_doc, ExtensionMethodType type) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Function::Entry entry;
  Function::Entry entry_kw;
  Function::Entry entry_ex;
  switch (callType(type)) {
    case ExtensionMethodType::kMethNoArgs:
      entry = methodTrampolineNoArgs;
      entry_kw = methodTrampolineNoArgsKw;
      entry_ex = methodTrampolineNoArgsEx;
      break;
    case ExtensionMethodType::kMethO:
      entry = methodTrampolineOneArg;
      entry_kw = methodTrampolineOneArgKw;
      entry_ex = methodTrampolineOneArgEx;
      break;
    case ExtensionMethodType::kMethVarArgs:
      entry = methodTrampolineVarArgs;
      entry_kw = methodTrampolineVarArgsKw;
      entry_ex = methodTrampolineVarArgsEx;
      break;
    case ExtensionMethodType::kMethVarArgsAndKeywords:
      entry = methodTrampolineKeywords;
      entry_kw = methodTrampolineKeywordsKw;
      entry_ex = methodTrampolineKeywordsEx;
      break;
    case ExtensionMethodType::kMethFastCall:
      entry = methodTrampolineFastCall;
      entry_kw = methodTrampolineFastCallKw;
      entry_ex = methodTrampolineFastCallEx;
      break;
    default:
      UNIMPLEMENTED("Unsupported MethodDef type");
  }
  Object name(&scope, runtime->newStrFromCStr(c_name));
  Object code(&scope, runtime->newIntFromCPtr(meth));
  Function function(&scope, runtime->newFunctionWithCustomEntry(
                                thread, name, code, entry, entry_kw, entry_ex));
  if (c_doc != nullptr) {
    function.setDoc(runtime->newStrFromCStr(c_doc));
  }
  if (isClassmethod(type)) {
    if (isStaticmethod(type)) {
      return thread->raiseWithFmt(LayoutId::kValueError,
                                  "method cannot be both class and static");
    }
    ClassMethod result(&scope, runtime->newClassMethod());
    result.setFunction(*function);
    return *result;
  }
  if (isStaticmethod(type)) {
    // TODO(T52962591): implement METH_STATIC
    UNIMPLEMENTED("C extension staticmethods");
  }
  return *function;
}

RawObject functionFromModuleMethodDef(Thread* thread, const char* c_name,
                                      void* meth, const char* c_doc,
                                      ExtensionMethodType type) {
  DCHECK(!isClassmethod(type), "module functions cannot set METH_CLASS");
  DCHECK(!isStaticmethod(type), "module functions cannot set METH_STATIC");
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Function::Entry entry;
  Function::Entry entry_kw;
  Function::Entry entry_ex;
  switch (callType(type)) {
    case ExtensionMethodType::kMethNoArgs:
      entry = moduleTrampolineNoArgs;
      entry_kw = moduleTrampolineNoArgsKw;
      entry_ex = moduleTrampolineNoArgsEx;
      break;
    case ExtensionMethodType::kMethO:
      entry = moduleTrampolineOneArg;
      entry_kw = moduleTrampolineOneArgKw;
      entry_ex = moduleTrampolineOneArgEx;
      break;
    case ExtensionMethodType::kMethVarArgs:
      entry = moduleTrampolineVarArgs;
      entry_kw = moduleTrampolineVarArgsKw;
      entry_ex = moduleTrampolineVarArgsEx;
      break;
    case ExtensionMethodType::kMethVarArgsAndKeywords:
      entry = moduleTrampolineKeywords;
      entry_kw = moduleTrampolineKeywordsKw;
      entry_ex = moduleTrampolineKeywordsEx;
      break;
    case ExtensionMethodType::kMethFastCall:
      entry = moduleTrampolineFastCall;
      entry_kw = moduleTrampolineFastCallKw;
      entry_ex = moduleTrampolineFastCallEx;
      break;
    default:
      UNIMPLEMENTED("Unsupported MethodDef type");
  }
  Object name(&scope, runtime->newStrFromCStr(c_name));
  Object code(&scope, runtime->newIntFromCPtr(meth));
  Function function(&scope, runtime->newFunctionWithCustomEntry(
                                thread, name, code, entry, entry_kw, entry_ex));
  if (c_doc != nullptr) {
    function.setDoc(runtime->newStrFromCStr(c_doc));
  }
  return *function;
}

RawObject functionGetAttribute(Thread* thread, const Function& function,
                               const Object& name_str, word hash) {
  // TODO(T39611261): Figure out a way to skip dict init.
  // Initialize Dict if non-existent
  if (function.dict().isNoneType()) {
    function.setDict(thread->runtime()->newDict());
  }

  return objectGetAttribute(thread, function, name_str, hash);
}

RawObject functionSetAttr(Thread* thread, const Function& function,
                          const Object& name_str, word hash,
                          const Object& value) {
  Runtime* runtime = thread->runtime();
  // Initialize Dict if non-existent
  HandleScope scope(thread);
  if (function.dict().isNoneType()) {
    function.setDict(runtime->newDict());
  }

  // TODO(T53626118) Raise an exception when `name_str` is a string subclass
  // that overrides `__eq__` or `__hash__`.
  Str name_underlying(&scope, strUnderlying(thread, name_str));
  Str name_interned(&scope, runtime->internStr(thread, name_underlying));
  AttributeInfo info;
  Layout layout(&scope, runtime->layoutAt(function.layoutId()));
  if (runtime->layoutFindAttribute(thread, layout, name_interned, &info)) {
    return instanceSetAttr(thread, function, name_interned, value);
  }
  Dict function_dict(&scope, function.dict());
  runtime->dictAtPut(thread, function_dict, name_str, hash, value);
  return NoneType::object();
}

const BuiltinMethod FunctionBuiltins::kBuiltinMethods[] = {
    {SymbolId::kDunderGet, dunderGet},
    {SymbolId::kDunderGetattribute, dunderGetattribute},
    {SymbolId::kDunderSetattr, dunderSetattr},
    {SymbolId::kSentinelId, nullptr},
};

const BuiltinAttribute FunctionBuiltins::kAttributes[] = {
    // TODO(T44845145) Support assignment to __code__.
    {SymbolId::kDunderCode, RawFunction::kCodeOffset,
     AttributeFlags::kReadOnly},
    {SymbolId::kDunderDoc, RawFunction::kDocOffset},
    {SymbolId::kDunderModule, RawFunction::kModuleOffset},
    {SymbolId::kDunderName, RawFunction::kNameOffset},
    {SymbolId::kDunderQualname, RawFunction::kQualnameOffset},
    {SymbolId::kDunderDict, RawFunction::kDictOffset},
    {SymbolId::kSentinelId, -1},
};

void FunctionBuiltins::postInitialize(Runtime*, const Type& new_type) {
  HandleScope scope;
  Layout layout(&scope, new_type.instanceLayout());
  layout.setOverflowAttributes(SmallInt::fromWord(RawFunction::kDictOffset));
}

RawObject FunctionBuiltins::dunderGet(Thread* thread, Frame* frame,
                                      word nargs) {
  HandleScope scope(thread);
  Arguments args(frame, nargs);
  Object self(&scope, args.get(0));
  if (!self.isFunction()) {
    return thread->raiseRequiresType(self, SymbolId::kFunction);
  }
  Object instance(&scope, args.get(1));
  // When `instance is None` return the plain function because we are doing a
  // lookup on a class.
  if (instance.isNoneType()) {
    // The unfortunate exception to the rule is looking up a descriptor on the
    // `None` object itself. We make it work by always returning a bound method
    // when `type is type(None)` and special casing the lookup of attributes of
    // `type(None)` to skip `__get__` in `Runtime::classGetAttr()`.
    Type type(&scope, args.get(2));
    if (type.builtinBase() != LayoutId::kNoneType) {
      return *self;
    }
  }
  return thread->runtime()->newBoundMethod(self, instance);
}

RawObject FunctionBuiltins::dunderGetattribute(Thread* thread, Frame* frame,
                                               word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object self_obj(&scope, args.get(0));
  Runtime* runtime = thread->runtime();
  if (!self_obj.isFunction()) {
    return thread->raiseRequiresType(self_obj, SymbolId::kFunction);
  }
  Function self(&scope, *self_obj);
  Object name(&scope, args.get(1));
  if (!runtime->isInstanceOfStr(*name)) {
    return thread->raiseWithFmt(
        LayoutId::kTypeError, "attribute name must be string, not '%T'", &name);
  }
  Object hash_obj(&scope, Interpreter::hash(thread, name));
  if (hash_obj.isErrorException()) return *hash_obj;
  word hash = SmallInt::cast(*hash_obj).value();
  Object result(&scope, functionGetAttribute(thread, self, name, hash));
  if (result.isErrorNotFound()) {
    Object function_name(&scope, self.name());
    return thread->raiseWithFmt(LayoutId::kAttributeError,
                                "function '%S' has no attribute '%S'",
                                &function_name, &name);
  }
  return *result;
}

RawObject FunctionBuiltins::dunderSetattr(Thread* thread, Frame* frame,
                                          word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object self_obj(&scope, args.get(0));
  Runtime* runtime = thread->runtime();
  if (!self_obj.isFunction()) {
    return thread->raiseRequiresType(self_obj, SymbolId::kFunction);
  }
  Function self(&scope, *self_obj);
  Object name(&scope, args.get(1));
  if (!runtime->isInstanceOfStr(*name)) {
    return thread->raiseWithFmt(
        LayoutId::kTypeError, "attribute name must be string, not '%T'", &name);
  }
  Object hash_obj(&scope, Interpreter::hash(thread, name));
  if (hash_obj.isErrorException()) return *hash_obj;
  word hash = SmallInt::cast(*hash_obj).value();
  Object value(&scope, args.get(2));
  return functionSetAttr(thread, self, name, hash, value);
}

const BuiltinAttribute BoundMethodBuiltins::kAttributes[] = {
    {SymbolId::kDunderFunc, RawBoundMethod::kFunctionOffset,
     AttributeFlags::kReadOnly},
    {SymbolId::kDunderSelf, RawBoundMethod::kSelfOffset,
     AttributeFlags::kReadOnly},
    {SymbolId::kSentinelId, 0},
};

}  // namespace py
