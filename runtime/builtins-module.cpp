#include "builtins-module.h"

#include <unistd.h>
#include <iostream>

#include "bytearray-builtins.h"
#include "bytes-builtins.h"
#include "complex-builtins.h"
#include "exception-builtins.h"
#include "file.h"
#include "frame.h"
#include "frozen-modules.h"
#include "globals.h"
#include "handles.h"
#include "int-builtins.h"
#include "interpreter.h"
#include "list-builtins.h"
#include "marshal.h"
#include "objects.h"
#include "runtime.h"
#include "str-builtins.h"
#include "thread.h"
#include "trampolines-inl.h"
#include "tuple-builtins.h"
#include "type-builtins.h"

namespace python {

std::ostream* builtinStdout = &std::cout;
std::ostream* builtinStderr = &std::cerr;

RawObject getAttribute(Thread* thread, const Object& self, const Object& name) {
  Runtime* runtime = thread->runtime();
  if (!runtime->isInstanceOfStr(*name)) {
    return thread->raiseTypeErrorWithCStr(
        "getattr(): attribute name must be string");
  }
  return runtime->attributeAt(thread, self, name);
}

RawObject hasAttribute(Thread* thread, const Object& self, const Object& name) {
  Runtime* runtime = thread->runtime();
  if (!runtime->isInstanceOfStr(*name)) {
    return thread->raiseTypeErrorWithCStr(
        "hasattr(): attribute name must be string");
  }

  HandleScope scope(thread);
  Object result(&scope, runtime->attributeAt(thread, self, name));
  if (!result.isError()) {
    return Bool::trueObj();
  }

  Type given(&scope, thread->pendingExceptionType());
  Type exc(&scope, runtime->typeAt(LayoutId::kAttributeError));
  if (givenExceptionMatches(thread, given, exc)) {
    thread->clearPendingException();
    return Bool::falseObj();
  }

  return Error::object();
}

RawObject setAttribute(Thread* thread, const Object& self, const Object& name,
                       const Object& value) {
  Runtime* runtime = thread->runtime();
  if (!runtime->isInstanceOfStr(*name)) {
    return thread->raiseTypeErrorWithCStr(
        "setattr(): attribute name must be string");
  }
  return runtime->attributeAtPut(thread, self, name, value);
}

const BuiltinMethod BuiltinsModule::kBuiltinMethods[] = {
    {SymbolId::kCallable, callable},
    {SymbolId::kChr, chr},
    {SymbolId::kCompile, compile},
    {SymbolId::kDivmod, divmod},
    {SymbolId::kDunderImport, dunderImport},
    {SymbolId::kExec, exec},
    {SymbolId::kGetattr, getattr},
    {SymbolId::kHasattr, hasattr},
    {SymbolId::kIsInstance, isinstance},
    {SymbolId::kIsSubclass, issubclass},
    {SymbolId::kOrd, ord},
    {SymbolId::kSetattr, setattr},
    {SymbolId::kUnderAddress, underAddress},
    {SymbolId::kUnderByteArrayJoin, ByteArrayBuiltins::join},
    {SymbolId::kUnderBytesJoin, BytesBuiltins::join},
    {SymbolId::kUnderComplexImag, complexGetImag},
    {SymbolId::kUnderComplexReal, complexGetReal},
    {SymbolId::kUnderListSort, underListSort},
    {SymbolId::kUnderPrintStr, underPrintStr},
    {SymbolId::kUnderReprEnter, underReprEnter},
    {SymbolId::kUnderReprLeave, underReprLeave},
    {SymbolId::kUnderStrEscapeNonAscii, underStrEscapeNonAscii},
    {SymbolId::kUnderStrFind, underStrFind},
    {SymbolId::kUnderStrRFind, underStrRFind},
    {SymbolId::kUnderStructseqGetAttr, underStructseqGetAttr},
    {SymbolId::kUnderStructseqSetAttr, underStructseqSetAttr},
    {SymbolId::kSentinelId, nullptr},
};

const BuiltinType BuiltinsModule::kBuiltinTypes[] = {
    {SymbolId::kArithmeticError, LayoutId::kArithmeticError},
    {SymbolId::kAssertionError, LayoutId::kAssertionError},
    {SymbolId::kAttributeError, LayoutId::kAttributeError},
    {SymbolId::kBaseException, LayoutId::kBaseException},
    {SymbolId::kBlockingIOError, LayoutId::kBlockingIOError},
    {SymbolId::kBool, LayoutId::kBool},
    {SymbolId::kBrokenPipeError, LayoutId::kBrokenPipeError},
    {SymbolId::kBufferError, LayoutId::kBufferError},
    {SymbolId::kByteArray, LayoutId::kByteArray},
    {SymbolId::kBytes, LayoutId::kBytes},
    {SymbolId::kBytesWarning, LayoutId::kBytesWarning},
    {SymbolId::kChildProcessError, LayoutId::kChildProcessError},
    {SymbolId::kClassmethod, LayoutId::kClassMethod},
    {SymbolId::kComplex, LayoutId::kComplex},
    {SymbolId::kConnectionAbortedError, LayoutId::kConnectionAbortedError},
    {SymbolId::kConnectionError, LayoutId::kConnectionError},
    {SymbolId::kConnectionRefusedError, LayoutId::kConnectionRefusedError},
    {SymbolId::kConnectionResetError, LayoutId::kConnectionResetError},
    {SymbolId::kCoroutine, LayoutId::kCoroutine},
    {SymbolId::kDeprecationWarning, LayoutId::kDeprecationWarning},
    {SymbolId::kDict, LayoutId::kDict},
    {SymbolId::kDictItemIterator, LayoutId::kDictItemIterator},
    {SymbolId::kDictItems, LayoutId::kDictItems},
    {SymbolId::kDictKeyIterator, LayoutId::kDictKeyIterator},
    {SymbolId::kDictKeys, LayoutId::kDictKeys},
    {SymbolId::kDictValueIterator, LayoutId::kDictValueIterator},
    {SymbolId::kDictValues, LayoutId::kDictValues},
    {SymbolId::kEOFError, LayoutId::kEOFError},
    {SymbolId::kException, LayoutId::kException},
    {SymbolId::kFileExistsError, LayoutId::kFileExistsError},
    {SymbolId::kFileNotFoundError, LayoutId::kFileNotFoundError},
    {SymbolId::kFloat, LayoutId::kFloat},
    {SymbolId::kFloatingPointError, LayoutId::kFloatingPointError},
    {SymbolId::kFrozenSet, LayoutId::kFrozenSet},
    {SymbolId::kFunction, LayoutId::kFunction},
    {SymbolId::kFutureWarning, LayoutId::kFutureWarning},
    {SymbolId::kGenerator, LayoutId::kGenerator},
    {SymbolId::kGeneratorExit, LayoutId::kGeneratorExit},
    {SymbolId::kImportError, LayoutId::kImportError},
    {SymbolId::kImportWarning, LayoutId::kImportWarning},
    {SymbolId::kIndentationError, LayoutId::kIndentationError},
    {SymbolId::kIndexError, LayoutId::kIndexError},
    {SymbolId::kInt, LayoutId::kInt},
    {SymbolId::kInterruptedError, LayoutId::kInterruptedError},
    {SymbolId::kIsADirectoryError, LayoutId::kIsADirectoryError},
    {SymbolId::kKeyError, LayoutId::kKeyError},
    {SymbolId::kKeyboardInterrupt, LayoutId::kKeyboardInterrupt},
    {SymbolId::kLargeInt, LayoutId::kLargeInt},
    {SymbolId::kList, LayoutId::kList},
    {SymbolId::kListIterator, LayoutId::kListIterator},
    {SymbolId::kLookupError, LayoutId::kLookupError},
    {SymbolId::kMemoryError, LayoutId::kMemoryError},
    {SymbolId::kMemoryView, LayoutId::kMemoryView},
    {SymbolId::kModule, LayoutId::kModule},
    {SymbolId::kModuleNotFoundError, LayoutId::kModuleNotFoundError},
    {SymbolId::kNameError, LayoutId::kNameError},
    {SymbolId::kNoneType, LayoutId::kNoneType},
    {SymbolId::kNotADirectoryError, LayoutId::kNotADirectoryError},
    {SymbolId::kNotImplementedError, LayoutId::kNotImplementedError},
    {SymbolId::kOSError, LayoutId::kOSError},
    {SymbolId::kObjectTypename, LayoutId::kObject},
    {SymbolId::kOverflowError, LayoutId::kOverflowError},
    {SymbolId::kPendingDeprecationWarning,
     LayoutId::kPendingDeprecationWarning},
    {SymbolId::kPermissionError, LayoutId::kPermissionError},
    {SymbolId::kProcessLookupError, LayoutId::kProcessLookupError},
    {SymbolId::kProperty, LayoutId::kProperty},
    {SymbolId::kRange, LayoutId::kRange},
    {SymbolId::kRangeIterator, LayoutId::kRangeIterator},
    {SymbolId::kRecursionError, LayoutId::kRecursionError},
    {SymbolId::kReferenceError, LayoutId::kReferenceError},
    {SymbolId::kResourceWarning, LayoutId::kResourceWarning},
    {SymbolId::kRuntimeError, LayoutId::kRuntimeError},
    {SymbolId::kRuntimeWarning, LayoutId::kRuntimeWarning},
    {SymbolId::kSet, LayoutId::kSet},
    {SymbolId::kSetIterator, LayoutId::kSetIterator},
    {SymbolId::kSlice, LayoutId::kSlice},
    {SymbolId::kSmallInt, LayoutId::kSmallInt},
    {SymbolId::kStaticMethod, LayoutId::kStaticMethod},
    {SymbolId::kStopAsyncIteration, LayoutId::kStopAsyncIteration},
    {SymbolId::kStopIteration, LayoutId::kStopIteration},
    {SymbolId::kStr, LayoutId::kStr},
    {SymbolId::kStrIterator, LayoutId::kStrIterator},
    {SymbolId::kSuper, LayoutId::kSuper},
    {SymbolId::kSyntaxError, LayoutId::kSyntaxError},
    {SymbolId::kSyntaxWarning, LayoutId::kSyntaxWarning},
    {SymbolId::kSystemError, LayoutId::kSystemError},
    {SymbolId::kSystemExit, LayoutId::kSystemExit},
    {SymbolId::kTabError, LayoutId::kTabError},
    {SymbolId::kTimeoutError, LayoutId::kTimeoutError},
    {SymbolId::kTuple, LayoutId::kTuple},
    {SymbolId::kTupleIterator, LayoutId::kTupleIterator},
    {SymbolId::kType, LayoutId::kType},
    {SymbolId::kTypeError, LayoutId::kTypeError},
    {SymbolId::kUnboundLocalError, LayoutId::kUnboundLocalError},
    {SymbolId::kUnicodeDecodeError, LayoutId::kUnicodeDecodeError},
    {SymbolId::kUnicodeEncodeError, LayoutId::kUnicodeEncodeError},
    {SymbolId::kUnicodeError, LayoutId::kUnicodeError},
    {SymbolId::kUnicodeTranslateError, LayoutId::kUnicodeTranslateError},
    {SymbolId::kUnicodeWarning, LayoutId::kUnicodeWarning},
    {SymbolId::kUserWarning, LayoutId::kUserWarning},
    {SymbolId::kValueError, LayoutId::kValueError},
    {SymbolId::kWarning, LayoutId::kWarning},
    {SymbolId::kZeroDivisionError, LayoutId::kZeroDivisionError},
    {SymbolId::kSentinelId, LayoutId::kSentinelId},
};

void BuiltinsModule::postInitialize(Thread* thread, Runtime* runtime,
                                    const Module& module) {
  runtime->build_class_ = runtime->moduleAddNativeFunction(
      module, SymbolId::kDunderBuildClass,
      nativeTrampoline<BuiltinsModule::buildClass>,
      nativeTrampolineKw<BuiltinsModule::buildClassKw>,
      unimplementedTrampoline);

  // _patch is not patched because that would cause a circularity problem.
  runtime->moduleAddNativeFunction(module, SymbolId::kUnderPatch,
                                   nativeTrampoline<BuiltinsModule::underPatch>,
                                   unimplementedTrampoline,
                                   unimplementedTrampoline);

  HandleScope scope(thread);
  Object not_implemented(&scope, runtime->notImplemented());
  runtime->moduleAddGlobal(module, SymbolId::kNotImplemented, not_implemented);

  Object unbound_value(&scope, runtime->unboundValue());
  runtime->moduleAddGlobal(module, SymbolId::kUnderUnboundValue, unbound_value);

  // For use in builtins :(
  Object stdout_val(&scope, SmallInt::fromWord(STDOUT_FILENO));
  runtime->moduleAddGlobal(module, SymbolId::kUnderStdout, stdout_val);

  if (runtime->executeModule(kBuiltinsModuleData, module).isError()) {
    thread->printPendingException();
    std::exit(EXIT_FAILURE);
  }

  // TODO(T39575976): Create a consistent way to remove from global dict
  // Explicitly remove module as this is not exposed in CPython
  Dict module_dict(&scope, module.dict());
  Object module_name(&scope, runtime->symbols()->Module());
  runtime->dictRemove(module_dict, module_name);

  Object dunder_import_name(&scope,
                            runtime->symbols()->at(SymbolId::kDunderImport));
  runtime->dunder_import_ = runtime->dictAt(module_dict, dunder_import_name);
}

RawObject BuiltinsModule::buildClass(Thread* thread, Frame* frame, word nargs) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);

  if (nargs < 2) {
    std::abort();  // TODO(cshapiro): throw a TypeError exception.
  }
  Arguments args(frame, nargs);
  if (!args.get(0).isFunction()) {
    std::abort();  // TODO(cshapiro): throw a TypeError exception.
  }
  if (!args.get(1).isStr()) {
    std::abort();  // TODO(cshapiro): throw a TypeError exception.
  }

  Function body(&scope, args.get(0));
  Object name(&scope, args.get(1));
  Tuple bases(&scope, runtime->newTuple(nargs - 2));
  for (word i = 0, j = 2; j < nargs; i++, j++) {
    bases.atPut(i, args.get(j));
  }

  // TODO(cshapiro): might need to do some kind of callback here and we want
  // backtraces to work correctly.  The key to doing that would be to put some
  // state on the stack in between the the incoming arguments from the builtin
  // caller and the on-stack state for the class body function call.
  Dict dict(&scope, runtime->newDict());
  thread->runClassFunction(body, dict);

  Type type(&scope, runtime->typeAt(LayoutId::kType));
  Function dunder_call(
      &scope, runtime->lookupSymbolInMro(thread, type, SymbolId::kDunderCall));
  frame->pushValue(*dunder_call);
  frame->pushValue(*type);
  frame->pushValue(*name);
  frame->pushValue(*bases);
  frame->pushValue(*dict);
  return Interpreter::call(thread, frame, 4);
}

static bool isPass(const Code& code) {
  HandleScope scope;
  Bytes bytes(&scope, code.code());
  // const_loaded is the index into the consts array that is returned
  word const_loaded = bytes.byteAt(1);
  return bytes.length() == 4 && bytes.byteAt(0) == LOAD_CONST &&
         RawTuple::cast(code.consts()).at(const_loaded).isNoneType() &&
         bytes.byteAt(2) == RETURN_VALUE && bytes.byteAt(3) == 0;
}

void copyFunctionEntries(Thread* thread, const Function& base,
                         const Function& patch) {
  HandleScope scope(thread);
  Str method_name(&scope, base.name());
  Code patch_code(&scope, patch.code());
  Code base_code(&scope, base.code());
  CHECK(isPass(patch_code),
        "Redefinition of native code method '%s' in managed code",
        method_name.toCStr());
  CHECK(!base_code.code().isNoneType(),
        "Useless declaration of native code method %s in managed code",
        method_name.toCStr());
  patch_code.setCode(base_code.code());
  base.setCode(*patch_code);
  patch.setEntry(base.entry());
  patch.setEntryKw(base.entryKw());
  patch.setEntryEx(base.entryEx());
}

void patchTypeDict(Thread* thread, const Dict& base, const Dict& patch) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  Tuple patch_data(&scope, patch.data());
  for (word i = Dict::Bucket::kFirst;
       Dict::Bucket::nextItem(*patch_data, &i);) {
    Str key(&scope, Dict::Bucket::key(*patch_data, i));
    Object patch_value_cell(&scope, Dict::Bucket::value(*patch_data, i));
    DCHECK(patch_value_cell.isValueCell(),
           "Values in type dict should be ValueCell");
    Object patch_obj(&scope, RawValueCell::cast(*patch_value_cell).value());

    // Copy function entries if the method already exists as a native builtin.
    Object base_obj(&scope, runtime->typeDictAt(base, key));
    if (!base_obj.isError()) {
      CHECK(patch_obj.isFunction(), "Python should only annotate functions");
      Function patch_fn(&scope, *patch_obj);
      CHECK(base_obj.isFunction(),
            "Python annotation of non-function native object");
      Function base_fn(&scope, *base_obj);

      copyFunctionEntries(thread, base_fn, patch_fn);
    }
    runtime->typeDictAtPut(base, key, patch_obj);
  }
}

RawObject BuiltinsModule::buildClassKw(Thread* thread, Frame* frame,
                                       word nargs) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  KwArguments args(frame, nargs);
  if (args.numArgs() < 2) {
    return thread->raiseTypeErrorWithCStr("not enough args for build class.");
  }
  if (!args.get(0).isFunction()) {
    return thread->raiseTypeErrorWithCStr("class body is not function.");
  }

  if (!args.get(1).isStr()) {
    return thread->raiseTypeErrorWithCStr("class name is not string.");
  }

  Object bootstrap(&scope, args.getKw(runtime->symbols()->Bootstrap()));
  if (bootstrap.isError()) {
    bootstrap = Bool::falseObj();
  }

  Object metaclass(&scope, args.getKw(runtime->symbols()->Metaclass()));
  if (metaclass.isError()) {
    metaclass = runtime->typeAt(LayoutId::kType);
  }

  Tuple bases(&scope,
              runtime->newTuple(args.numArgs() - args.numKeywords() - 1));
  for (word i = 0, j = 2; j < args.numArgs(); i++, j++) {
    bases.atPut(i, args.get(j));
  }

  Dict type_dict(&scope, runtime->newDict());
  Function body(&scope, args.get(0));
  Str name(&scope, args.get(1));
  if (*bootstrap == Bool::falseObj()) {
    // An ordinary class initialization creates a new class dictionary.
    thread->runClassFunction(body, type_dict);
  } else {
    // A bootstrap class initialization uses the existing class dictionary.
    CHECK(frame->previousFrame() != nullptr, "must have a caller frame");
    Dict globals(&scope, frame->previousFrame()->globals());
    Object type_obj(&scope, runtime->moduleDictAt(globals, name));
    CHECK(type_obj.isType(),
          "Name '%s' is not bound to a type object. "
          "You may need to add it to the builtins module.",
          name.toCStr());
    Type type(&scope, *type_obj);
    type_dict = type.dict();

    Dict patch_type(&scope, runtime->newDict());
    thread->runClassFunction(body, patch_type);
    patchTypeDict(thread, type_dict, patch_type);
    // A bootstrap type initialization is complete at this point.
    return *type;
  }

  Type type(&scope, *metaclass);
  Function dunder_call(
      &scope, runtime->lookupSymbolInMro(thread, type, SymbolId::kDunderCall));
  frame->pushValue(*dunder_call);
  frame->pushValue(*type);
  frame->pushValue(*name);
  frame->pushValue(*bases);
  frame->pushValue(*type_dict);
  return Interpreter::call(thread, frame, 4);
}

RawObject BuiltinsModule::callable(Thread* thread, Frame* frame, word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object arg(&scope, args.get(0));
  return Bool::fromBool(thread->runtime()->isCallable(thread, arg));
}

RawObject BuiltinsModule::chr(Thread* thread, Frame* frame_frame, word nargs) {
  Arguments args(frame_frame, nargs);
  RawObject arg = args.get(0);
  if (!arg.isSmallInt()) {
    return thread->raiseTypeErrorWithCStr("Unsupported type in builtin 'chr'");
  }
  word w = RawSmallInt::cast(arg).value();
  const char s[2]{static_cast<char>(w), 0};
  return SmallStr::fromCStr(s);
}

static RawObject compileToBytecode(Thread* thread, const char* source) {
  HandleScope scope(thread);
  std::unique_ptr<char[]> bytecode_str(Runtime::compileFromCStr(source));
  Marshal::Reader reader(&scope, thread->runtime(), bytecode_str.get());
  reader.readLong();  // magic
  reader.readLong();  // mtime
  reader.readLong();  // size
  return reader.readObject();
}

static RawObject compileBytes(Thread* thread, const Bytes& source) {
  word bytes_len = source.length();
  unique_c_ptr<byte[]> source_bytes(
      static_cast<byte*>(std::malloc(bytes_len + 1)));
  source.copyTo(source_bytes.get(), bytes_len);
  source_bytes[bytes_len] = '\0';
  return compileToBytecode(thread, reinterpret_cast<char*>(source_bytes.get()));
}

static RawObject compileStr(Thread* thread, const Str& source) {
  unique_c_ptr<char[]> source_str(source.toCStr());
  return compileToBytecode(thread, source_str.get());
}

RawObject BuiltinsModule::compile(Thread* thread, Frame* frame, word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  // TODO(T40808881): Add compile support for bytearray, buffer, and subclasses
  Object data(&scope, args.get(0));
  if (!data.isStr() && !data.isBytes()) {
    return thread->raiseTypeErrorWithCStr(
        "compile() currently only supports a str or bytes source");
  }
  Str filename(&scope, args.get(1));
  Str mode(&scope, args.get(2));
  // TODO(emacs): Refactor into sane argument-fetching code
  if (args.get(3) != SmallInt::fromWord(0)) {  // not the default
    return thread->raiseTypeErrorWithCStr(
        "compile() does not yet support user-supplied flags");
  }
  // TODO(T40872645): Add support for compiler flag forwarding
  if (args.get(4) == Bool::falseObj()) {
    return thread->raiseTypeErrorWithCStr(
        "compile() does not yet support compiler flag forwarding");
  }
  if (args.get(5) != SmallInt::fromWord(-1)) {  // not the default
    return thread->raiseTypeErrorWithCStr(
        "compile() does not yet support user-supplied optimize");
  }
  // Note: mode doesn't actually do anything yet.
  if (!mode.equalsCStr("exec") && !mode.equalsCStr("eval") &&
      !mode.equalsCStr("single")) {
    return thread->raiseValueErrorWithCStr(
        "Expected mode to be 'exec', 'eval', or 'single' in 'compile'");
  }

  Object code_obj(&scope, NoneType::object());
  if (data.isStr()) {
    Str source_str(&scope, *data);
    code_obj = compileStr(thread, source_str);
  } else {
    Bytes source_bytes(&scope, *data);
    code_obj = compileBytes(thread, source_bytes);
  }
  Code code(&scope, *code_obj);
  code.setFilename(*filename);
  return *code;
}

RawObject BuiltinsModule::divmod(Thread*, Frame*, word) {
  UNIMPLEMENTED("divmod(a, b)");
}

RawObject BuiltinsModule::exec(Thread* thread, Frame* frame, word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object source_obj(&scope, args.get(0));
  if (!source_obj.isCode() && !source_obj.isStr()) {
    return thread->raiseTypeErrorWithCStr(
        "Expected 'source' to be str or code in 'exec'");
  }
  // Per the docs:
  //   In all cases, if the optional parts are omitted, the code is executed in
  //   the current scope. If only globals is provided, it must be a dictionary,
  //   which will be used for both the global and the local variables.
  Object globals_obj(&scope, args.get(1));
  Object locals(&scope, args.get(2));
  Runtime* runtime = thread->runtime();
  if (globals_obj.isNoneType() &&
      locals.isNoneType()) {  // neither globals nor locals are provided
    Frame* caller_frame = frame->previousFrame();
    globals_obj = caller_frame->globals();
    DCHECK(globals_obj.isDict(),
           "Expected caller_frame->globals() to be dict in 'exec'");
    if (caller_frame->globals() != caller_frame->implicitGlobals()) {
      // TODO(T37888835): Fix 1 argument case
      // globals == implicitGlobals when code is being executed in a module
      // context. If we're not in a module context, this case is unimplemented.
      UNIMPLEMENTED("exec() with 1 argument not at the module level");
    }
    locals = *globals_obj;
  } else if (!globals_obj.isNoneType()) {  // only globals is provided
    if (!runtime->isInstanceOfDict(*globals_obj)) {
      return thread->raiseTypeErrorWithCStr(
          "Expected 'globals' to be dict in 'exec'");
    }
    locals = *globals_obj;
  } else {  // both globals and locals are provided
    if (!runtime->isInstanceOfDict(*globals_obj)) {
      return thread->raiseTypeErrorWithCStr(
          "Expected 'globals' to be dict in 'exec'");
    }
    if (!runtime->isMapping(thread, locals)) {
      return thread->raiseTypeErrorWithCStr(
          "Expected 'locals' to be a mapping in 'exec'");
    }
    // TODO(T37888835): Fix 3 argument case
    UNIMPLEMENTED("exec() with both globals and locals");
  }
  if (source_obj.isStr()) {
    Str source(&scope, *source_obj);
    source_obj = compileStr(thread, source);
    DCHECK(source_obj.isCode(), "compileStr must return code object");
  }
  Code code(&scope, *source_obj);
  if (code.numFreevars() != 0) {
    return thread->raiseTypeErrorWithCStr(
        "Expected 'source' not to have free variables in 'exec'");
  }
  Dict globals(&scope, *globals_obj);
  return thread->exec(code, globals, locals);
}

static RawObject isinstanceImpl(Thread* thread, const Object& obj,
                                const Object& type_obj) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);

  if (runtime->isInstanceOfType(*type_obj)) {
    Type type(&scope, *type_obj);
    return Bool::fromBool(runtime->isInstance(obj, type));
  }

  if (runtime->isInstanceOfTuple(*type_obj)) {
    Tuple types(&scope, *type_obj);
    Object elem(&scope, NoneType::object());
    Object result(&scope, NoneType::object());
    for (word i = 0, len = types.length(); i < len; i++) {
      elem = types.at(i);
      result = isinstanceImpl(thread, obj, elem);
      if (result.isError() || result == Bool::trueObj()) return *result;
    }
    return Bool::falseObj();
  }

  return thread->raiseTypeErrorWithCStr(
      "isinstance() arg 2 must be a type or tuple of types");
}

// TODO(mpage): isinstance (somewhat unsurprisingly at this point I guess) is
// actually far more complicated than one might expect. This is enough to get
// richards working.
RawObject BuiltinsModule::isinstance(Thread* thread, Frame* frame, word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object obj(&scope, args.get(0));
  Object type(&scope, args.get(1));
  return isinstanceImpl(thread, obj, type);
}

RawObject BuiltinsModule::issubclass(Thread* thread, Frame* frame, word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  if (!args.get(0).isType()) {
    return thread->raiseTypeErrorWithCStr("issubclass arg 1 must be a type");
  }
  Type type(&scope, args.get(0));
  Object classinfo(&scope, args.get(1));
  if (runtime->isInstanceOfType(*classinfo)) {
    Type possible_superclass(&scope, *classinfo);
    return Bool::fromBool(runtime->isSubclass(type, possible_superclass));
  }
  // If classinfo is not a tuple, then throw a TypeError.
  if (!classinfo.isTuple()) {
    return thread->raiseTypeErrorWithCStr(
        "issubclass() arg 2 must be a class of tuple of classes");
  }
  // If classinfo is a tuple, try each of the values, and return
  // True if the first argument is a subclass of any of them.
  Tuple tuple_of_types(&scope, *classinfo);
  for (word i = 0; i < tuple_of_types.length(); i++) {
    // If any argument is not a type, then throw TypeError.
    if (!runtime->isInstanceOfType(tuple_of_types.at(i))) {
      return thread->raiseTypeErrorWithCStr(
          "issubclass() arg 2 must be a class of tuple of classes");
    }
    Type possible_superclass(&scope, tuple_of_types.at(i));
    // If any of the types are a superclass, return true.
    if (runtime->isSubclass(type, possible_superclass)) return Bool::trueObj();
  }
  // None of the types in the tuple were a superclass, so return false.
  return Bool::falseObj();
}

RawObject BuiltinsModule::ord(Thread* thread, Frame* frame_frame, word nargs) {
  Arguments args(frame_frame, nargs);
  RawObject arg = args.get(0);
  if (!arg.isStr()) {
    return thread->raiseTypeErrorWithCStr("Unsupported type in builtin 'ord'");
  }
  auto str = RawStr::cast(arg);
  if (str.length() != 1) {
    return thread->raiseTypeErrorWithCStr(
        "Builtin 'ord' expects string of length 1");
  }
  return SmallInt::fromWord(str.charAt(0));
}

RawObject BuiltinsModule::dunderImport(Thread* thread, Frame* frame,
                                       word nargs) {
  HandleScope scope(thread);
  Arguments args(frame, nargs);
  Object name(&scope, args.get(0));
  Object globals(&scope, args.get(1));
  Object locals(&scope, args.get(2));
  Object fromlist(&scope, args.get(3));
  Object level(&scope, args.get(4));

  Runtime* runtime = thread->runtime();
  if (level.isInt() && RawInt::cast(*level).isZero()) {
    Object cached_module(&scope, runtime->findModule(name));
    if (!cached_module.isNoneType()) {
      return *cached_module;
    }
  }

  Object importlib_obj(
      &scope, runtime->findModuleById(SymbolId::kUnderFrozenImportlib));
  // We may need to load and create `_frozen_importlib` if it doesn't exist.
  if (importlib_obj.isNoneType()) {
    runtime->createImportlibModule();
    importlib_obj = runtime->findModuleById(SymbolId::kUnderFrozenImportlib);
  }
  Module importlib(&scope, *importlib_obj);

  Object dunder_import(
      &scope, runtime->moduleAtById(importlib, SymbolId::kDunderImport));
  if (dunder_import.isError()) return *dunder_import;

  return thread->invokeFunction5(SymbolId::kUnderFrozenImportlib,
                                 SymbolId::kDunderImport, name, globals, locals,
                                 fromlist, level);
}
RawObject BuiltinsModule::underListSort(Thread* thread, Frame* frame_frame,
                                        word nargs) {
  Arguments args(frame_frame, nargs);
  HandleScope scope(thread);
  CHECK(thread->runtime()->isInstanceOfList(args.get(0)),
        "Unsupported argument type for 'ls'");
  List list(&scope, args.get(0));
  return listSort(thread, list);
}

RawObject BuiltinsModule::underPrintStr(Thread* thread, Frame* frame_frame,
                                        word nargs) {
  Arguments args(frame_frame, nargs);
  HandleScope scope(thread);
  CHECK(args.get(0).isStr(), "Unsupported argument type for 'obj'");
  Str str(&scope, args.get(0));
  Object file(&scope, args.get(1));
  return fileWriteObjectStr(thread, file, str);
}

// TODO(T39322942): Turn this into the Range constructor (__init__ or __new__)
RawObject BuiltinsModule::getattr(Thread* thread, Frame* frame, word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object self(&scope, args.get(0));
  Object name(&scope, args.get(1));
  Object default_obj(&scope, args.get(2));
  Object result(&scope, getAttribute(thread, self, name));
  Runtime* runtime = thread->runtime();
  if (result.isError() && !default_obj.isUnboundValue()) {
    Type given(&scope, thread->pendingExceptionType());
    Type exc(&scope, runtime->typeAt(LayoutId::kAttributeError));
    if (givenExceptionMatches(thread, given, exc)) {
      thread->clearPendingException();
      result = *default_obj;
    }
  }
  return *result;
}

RawObject BuiltinsModule::hasattr(Thread* thread, Frame* frame, word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object self(&scope, args.get(0));
  Object name(&scope, args.get(1));
  return hasAttribute(thread, self, name);
}

RawObject BuiltinsModule::setattr(Thread* thread, Frame* frame, word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object self(&scope, args.get(0));
  Object name(&scope, args.get(1));
  Object value(&scope, args.get(2));
  return setAttribute(thread, self, name, value);
}

RawObject BuiltinsModule::underAddress(Thread* thread, Frame* frame,
                                       word nargs) {
  Arguments args(frame, nargs);
  return thread->runtime()->newInt(args.get(0).raw());
}

RawObject BuiltinsModule::underPatch(Thread* thread, Frame* frame, word nargs) {
  HandleScope scope(thread);
  Arguments args(frame, nargs);
  if (nargs != 1) {
    return thread->raiseTypeErrorWithCStr("_patch expects 1 argument");
  }

  Object patch_fn_obj(&scope, args.get(0));
  if (!patch_fn_obj.isFunction()) {
    return thread->raiseTypeErrorWithCStr("_patch expects function argument");
  }
  Function patch_fn(&scope, *patch_fn_obj);
  Str fn_name(&scope, patch_fn.name());
  Runtime* runtime = thread->runtime();
  Object module_name(&scope, patch_fn.module());
  Module module(&scope, runtime->findModule(module_name));
  Object base_fn_obj(&scope, runtime->moduleAt(module, fn_name));
  if (!base_fn_obj.isFunction()) {
    return thread->raiseTypeErrorWithCStr("_patch can only patch functions");
  }
  Function base_fn(&scope, *base_fn_obj);
  copyFunctionEntries(thread, base_fn, patch_fn);
  return *patch_fn;
}

RawObject BuiltinsModule::underReprEnter(Thread* thread, Frame* frame,
                                         word nargs) {
  HandleScope scope(thread);
  Arguments args(frame, nargs);
  Object obj(&scope, args.get(0));
  return thread->reprEnter(obj);
}

RawObject BuiltinsModule::underReprLeave(Thread* thread, Frame* frame,
                                         word nargs) {
  HandleScope scope(thread);
  Arguments args(frame, nargs);
  Object obj(&scope, args.get(0));
  thread->reprLeave(obj);
  return NoneType::object();
}

RawObject BuiltinsModule::underStrEscapeNonAscii(Thread* thread, Frame* frame,
                                                 word nargs) {
  HandleScope scope(thread);
  Arguments args(frame, nargs);
  CHECK(thread->runtime()->isInstanceOfStr(args.get(0)),
        "_str_escape_non_ascii expected str instance");
  Str obj(&scope, args.get(0));
  return strEscapeNonASCII(thread, obj);
}

RawObject BuiltinsModule::underStrFind(Thread* thread, Frame* frame,
                                       word nargs) {
  Runtime* runtime = thread->runtime();
  Arguments args(frame, nargs);
  DCHECK(runtime->isInstanceOfStr(args.get(0)),
         "_str_find requires 'str' instance");
  DCHECK(runtime->isInstanceOfStr(args.get(1)),
         "_str_find requires 'str' instance");
  HandleScope scope(thread);
  Str haystack(&scope, args.get(0));
  Str needle(&scope, args.get(1));
  Object start_obj(&scope, args.get(2));
  Object end_obj(&scope, args.get(3));
  word start =
      start_obj.isNoneType() ? 0 : RawInt::cast(*start_obj).asWordSaturated();
  word end = end_obj.isNoneType() ? kMaxWord
                                  : RawInt::cast(*end_obj).asWordSaturated();
  return strFind(haystack, needle, start, end);
}

RawObject BuiltinsModule::underStrRFind(Thread* thread, Frame* frame,
                                        word nargs) {
  Runtime* runtime = thread->runtime();
  Arguments args(frame, nargs);
  DCHECK(runtime->isInstanceOfStr(args.get(0)),
         "_str_rfind requires 'str' instance");
  DCHECK(runtime->isInstanceOfStr(args.get(1)),
         "_str_rfind requires 'str' instance");
  HandleScope scope(thread);
  Str haystack(&scope, args.get(0));
  Str needle(&scope, args.get(1));
  Object start_obj(&scope, args.get(2));
  Object end_obj(&scope, args.get(3));
  word start =
      start_obj.isNoneType() ? 0 : RawInt::cast(*start_obj).asWordSaturated();
  word end = end_obj.isNoneType() ? kMaxWord
                                  : RawInt::cast(*end_obj).asWordSaturated();
  return strRFind(haystack, needle, start, end);
}

}  // namespace python
