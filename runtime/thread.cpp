#include "thread.h"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "builtins-module.h"
#include "exception-builtins.h"
#include "frame.h"
#include "globals.h"
#include "handles.h"
#include "interpreter.h"
#include "module-builtins.h"
#include "objects.h"
#include "runtime.h"
#include "tuple-builtins.h"
#include "type-builtins.h"
#include "visitor.h"

namespace python {

void Handles::visitPointers(PointerVisitor* visitor) {
  for (Object* handle = head_; handle != nullptr;
       handle = handle->nextHandle()) {
    visitor->visitPointer(handle);
  }
}

thread_local Thread* Thread::current_thread_ = nullptr;

Thread::Thread(word size)
    : size_(Utils::roundUp(size, kPointerSize)),
      next_(nullptr),
      runtime_(nullptr),
      pending_exc_type_(NoneType::object()),
      pending_exc_value_(NoneType::object()),
      pending_exc_traceback_(NoneType::object()),
      caught_exc_stack_(NoneType::object()),
      api_repr_list_(NoneType::object()) {
  start_ = new byte[size]();  // Zero-initialize the stack
  // Stack growns down in order to match machine convention
  end_ = start_ + size;
  currentFrame_ = pushInitialFrame();
}

Thread::~Thread() { delete[] start_; }

void Thread::visitRoots(PointerVisitor* visitor) {
  visitStackRoots(visitor);
  handles()->visitPointers(visitor);
  visitor->visitPointer(&api_repr_list_);
  visitor->visitPointer(&pending_exc_type_);
  visitor->visitPointer(&pending_exc_value_);
  visitor->visitPointer(&pending_exc_traceback_);
  visitor->visitPointer(&caught_exc_stack_);
}

void Thread::visitStackRoots(PointerVisitor* visitor) {
  auto address = reinterpret_cast<uword>(stackPtr());
  auto end = reinterpret_cast<uword>(end_);
  std::memset(start_, 0, reinterpret_cast<byte*>(address) - start_);
  for (; address < end; address += kPointerSize) {
    visitor->visitPointer(reinterpret_cast<RawObject*>(address));
  }
}

Thread* Thread::current() { return Thread::current_thread_; }

void Thread::setCurrentThread(Thread* thread) {
  Thread::current_thread_ = thread;
}

byte* Thread::stackPtr() {
  return reinterpret_cast<byte*>(currentFrame_->valueStackTop());
}

inline Frame* Thread::openAndLinkFrame(word num_args, word num_vars,
                                       word stack_depth) {
  DCHECK(num_args >= 0, "must have 0 or more arguments");
  DCHECK(num_vars >= 0, "must have 0 or more locals");
  DCHECK(stack_depth >= 0, "stack depth cannot be negative");

  if (UNLIKELY(wouldStackOverflow(Frame::kSize +
                                  (num_vars + stack_depth) * kPointerSize))) {
    return nullptr;
  }

  // Initialize the frame.
  byte* new_sp = stackPtr() - num_vars * kPointerSize - Frame::kSize;
  Frame* frame = reinterpret_cast<Frame*>(new_sp);
  frame->init(num_args + num_vars);

  // return a pointer to the base of the frame
  linkFrame(frame);
  DCHECK(frame->function().totalLocals() == num_args + num_vars,
         "local counts mismatched");
  DCHECK(frame->isInvalid() == nullptr, "invalid frame");
  return frame;
}

void Thread::linkFrame(Frame* frame) {
  frame->setPreviousFrame(currentFrame_);
  currentFrame_ = frame;
}

bool Thread::wouldStackOverflow(word max_size) {
  // Check that there is sufficient space on the stack
  // TODO(T36407214): Grow stack
  byte* sp = stackPtr();
  if (LIKELY(sp - max_size >= start_)) {
    return false;
  }
  raiseWithFmt(LayoutId::kRecursionError, "maximum recursion depth exceeded");
  return true;
}

Frame* Thread::pushNativeFrame(word nargs) {
  // TODO(T36407290): native frames push arguments onto the stack when calling
  // back into the interpreter, but we can't statically know how much stack
  // space they will need. We may want to extend the api for such native calls
  // to include a declaration of how much space is needed. However, that's of
  // limited use right now since we can't detect an "overflow" of a frame
  // anyway.
  return openAndLinkFrame(nargs, 0, 0);
}

Frame* Thread::pushCallFrame(RawFunction function) {
  Frame* result = openAndLinkFrame(function.totalArgs(), function.totalVars(),
                                   function.stacksize());
  if (UNLIKELY(result == nullptr)) {
    return nullptr;
  }
  result->setBytecode(MutableBytes::cast(function.rewrittenBytecode()));
  result->setCaches(Tuple::cast(function.caches()));
  result->setVirtualPC(0);
  return result;
}

Frame* Thread::pushClassFunctionFrame(const Function& function) {
  HandleScope scope(this);
  Frame* result = pushCallFrame(*function);
  if (UNLIKELY(result == nullptr)) {
    return nullptr;
  }
  Code code(&scope, function.code());

  word num_locals = code.nlocals();
  word num_cellvars = code.numCellvars();
  DCHECK(code.cell2arg().isNoneType(), "class body cannot have cell2arg.");
  for (word i = 0; i < code.numCellvars(); i++) {
    result->setLocal(num_locals + i, runtime()->newValueCell());
  }

  // initialize free vars
  DCHECK(code.numFreevars() == 0 ||
             code.numFreevars() ==
                 Tuple::cast(Function::cast(*function).closure()).length(),
         "Number of freevars is different than the closure.");
  for (word i = 0; i < code.numFreevars(); i++) {
    result->setLocal(num_locals + num_cellvars + i,
                     Tuple::cast(Function::cast(*function).closure()).at(i));
  }
  return result;
}

Frame* Thread::pushInitialFrame() {
  byte* sp = end_ - Frame::kSize;
  CHECK(sp > start_, "no space for initial frame");
  Frame* frame = reinterpret_cast<Frame*>(sp);
  frame->init(0);
  frame->setPreviousFrame(nullptr);
  return frame;
}

Frame* Thread::popFrame() {
  Frame* frame = currentFrame_;
  DCHECK(!frame->isSentinel(), "cannot pop initial frame");
  currentFrame_ = frame->previousFrame();
  return currentFrame_;
}

RawObject Thread::exec(const Code& code, const Dict& globals,
                       const Object& locals) {
  HandleScope scope(this);
  Object qualname(&scope, Str::empty());

  CHECK(!code.hasOptimizedOrNewLocals(),
        "exec() code must not have CO_OPTIMIZED or CO_NEWLOCALS");

  Runtime* runtime = this->runtime();
  Object builtins_module_obj(
      &scope, moduleDictAtById(this, globals, SymbolId::kDunderBuiltins));
  if (builtins_module_obj.isErrorNotFound()) {
    builtins_module_obj = runtime->findModuleById(SymbolId::kBuiltins);
    DCHECK(!builtins_module_obj.isNoneType(), "invalid builtins module");
    moduleDictAtPutById(this, globals, SymbolId::kDunderBuiltins,
                        builtins_module_obj);
  }

  Function function(
      &scope, runtime->newFunctionWithCode(this, qualname, code, globals));
  // Push implicit globals.
  currentFrame()->pushValue(*locals);
  // Push function to be available from frame.function().
  currentFrame()->pushValue(*function);
  if (UNLIKELY(pushCallFrame(*function) == nullptr)) {
    return Error::exception();
  }
  Object result(&scope, Interpreter::execute(this));
  DCHECK(currentFrame()->topValue() == function, "stack mismatch");
  DCHECK(currentFrame()->peek(1) == *locals, "stack mismatch");
  currentFrame()->dropValues(2);
  return *result;
}

RawObject Thread::runClassFunction(const Function& function, const Dict& dict) {
  CHECK(!function.hasOptimizedOrNewLocals(),
        "runClassFunction() code must not have CO_OPTIMIZED or CO_NEWLOCALS");

  HandleScope scope(this);
  // Push implicit globals and function.
  currentFrame()->pushValue(*dict);
  currentFrame()->pushValue(*function);
  if (UNLIKELY(pushClassFunctionFrame(function) == nullptr)) {
    return Error::exception();
  }
  Object result(&scope, Interpreter::execute(this));
  DCHECK(currentFrame()->topValue() == function, "stack mismatch");
  DCHECK(currentFrame()->peek(1) == *dict, "stack mismatch");
  currentFrame()->dropValues(2);
  return *result;
}

RawObject Thread::invokeMethod1(const Object& receiver, SymbolId selector) {
  HandleScope scope(this);
  Object method(&scope, Interpreter::lookupMethod(this, currentFrame_, receiver,
                                                  selector));
  if (method.isError()) return *method;
  return Interpreter::callMethod1(this, currentFrame_, method, receiver);
}

RawObject Thread::invokeMethod2(const Object& receiver, SymbolId selector,
                                const Object& arg1) {
  HandleScope scope(this);
  Object method(&scope, Interpreter::lookupMethod(this, currentFrame_, receiver,
                                                  selector));
  if (method.isError()) return *method;
  return Interpreter::callMethod2(this, currentFrame_, method, receiver, arg1);
}

RawObject Thread::invokeMethod3(const Object& receiver, SymbolId selector,
                                const Object& arg1, const Object& arg2) {
  HandleScope scope(this);
  Object method(&scope, Interpreter::lookupMethod(this, currentFrame_, receiver,
                                                  selector));
  if (method.isError()) return *method;
  return Interpreter::callMethod3(this, currentFrame_, method, receiver, arg1,
                                  arg2);
}

RawObject Thread::invokeMethodStatic1(LayoutId type, SymbolId method_name,
                                      const Object& receiver) {
  HandleScope scope(this);
  Object type_obj(&scope, runtime()->typeAt(type));
  if (type_obj.isError()) return *type_obj;
  Type type_handle(&scope, *type_obj);
  Object method(&scope, typeLookupInMroById(this, type_handle, method_name));
  if (method.isError()) return *method;
  return Interpreter::callMethod1(this, currentFrame_, method, receiver);
}

RawObject Thread::invokeMethodStatic2(LayoutId type, SymbolId method_name,
                                      const Object& receiver,
                                      const Object& arg1) {
  HandleScope scope(this);
  Object type_obj(&scope, runtime()->typeAt(type));
  if (type_obj.isError()) return *type_obj;
  Type type_handle(&scope, *type_obj);
  Object method(&scope, typeLookupInMroById(this, type_handle, method_name));
  if (method.isError()) return *method;
  return Interpreter::callMethod2(this, currentFrame_, method, receiver, arg1);
}

RawObject Thread::invokeMethodStatic3(LayoutId type, SymbolId method_name,
                                      const Object& receiver,
                                      const Object& arg1, const Object& arg2) {
  HandleScope scope(this);
  Object type_obj(&scope, runtime()->typeAt(type));
  if (type_obj.isError()) return *type_obj;
  Type type_handle(&scope, *type_obj);
  Object method(&scope, typeLookupInMroById(this, type_handle, method_name));
  if (method.isError()) return *method;
  return Interpreter::callMethod3(this, currentFrame_, method, receiver, arg1,
                                  arg2);
}

RawObject Thread::invokeMethodStatic4(LayoutId type, SymbolId method_name,
                                      const Object& receiver,
                                      const Object& arg1, const Object& arg2,
                                      const Object& arg3) {
  HandleScope scope(this);
  Object type_obj(&scope, runtime()->typeAt(type));
  if (type_obj.isError()) return *type_obj;
  Type type_handle(&scope, *type_obj);
  Object method(&scope, typeLookupInMroById(this, type_handle, method_name));
  if (method.isError()) return *method;
  return Interpreter::callMethod4(this, currentFrame_, method, receiver, arg1,
                                  arg2, arg3);
}

RawObject Thread::invokeFunction1(SymbolId module, SymbolId name,
                                  const Object& arg1) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::callFunction1(this, currentFrame_, func, arg1);
}

RawObject Thread::invokeFunction2(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::callFunction2(this, currentFrame_, func, arg1, arg2);
}

RawObject Thread::invokeFunction3(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2,
                                  const Object& arg3) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::callFunction3(this, currentFrame_, func, arg1, arg2,
                                    arg3);
}

RawObject Thread::invokeFunction4(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2,
                                  const Object& arg3, const Object& arg4) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::callFunction4(this, currentFrame_, func, arg1, arg2, arg3,
                                    arg4);
}

RawObject Thread::invokeFunction5(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2,
                                  const Object& arg3, const Object& arg4,
                                  const Object& arg5) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::callFunction5(this, currentFrame_, func, arg1, arg2, arg3,
                                    arg4, arg5);
}

RawObject Thread::invokeFunction6(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2,
                                  const Object& arg3, const Object& arg4,
                                  const Object& arg5, const Object& arg6) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::callFunction6(this, currentFrame_, func, arg1, arg2, arg3,
                                    arg4, arg5, arg6);
}

RawObject Thread::raise(LayoutId type, RawObject value) {
  return raiseWithType(runtime()->typeAt(type), value);
}

RawObject Thread::raiseWithType(RawObject type, RawObject value) {
  DCHECK(!hasPendingException(), "unhandled exception lingering");
  HandleScope scope(this);
  Type type_obj(&scope, type);
  Object value_obj(&scope, value);
  Object traceback_obj(&scope, NoneType::object());

  value_obj = chainExceptionContext(type_obj, value_obj);
  if (value_obj.isErrorException()) return Error::exception();

  setPendingExceptionType(*type_obj);
  setPendingExceptionValue(*value_obj);
  setPendingExceptionTraceback(*traceback_obj);
  return Error::exception();
}

RawObject Thread::chainExceptionContext(const Type& type, const Object& value) {
  if (caughtExceptionType().isNoneType() ||
      caughtExceptionValue().isNoneType()) {
    return *value;
  }

  HandleScope scope(this);
  Object fixed_value(&scope, *value);
  if (!runtime()->isInstanceOfBaseException(*value)) {
    // Perform partial normalization before attempting to set __context__.
    fixed_value = createException(this, type, value);
    if (fixed_value.isError()) return *fixed_value;
  }

  // Avoid creating cycles by breaking any link from caught_value to value
  // before setting value's __context__.
  BaseException caught_value(&scope, caughtExceptionValue());
  if (*fixed_value == *caught_value) return *fixed_value;
  BaseException exc(&scope, *caught_value);
  Object context(&scope, NoneType::object());
  while (!(context = exc.context()).isNoneType()) {
    if (*context == *fixed_value) {
      exc.setContext(Unbound::object());
      break;
    }
    exc = *context;
  }

  BaseException(&scope, *fixed_value).setContext(*caught_value);
  return *fixed_value;
}

RawObject Thread::raiseWithFmt(LayoutId type, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  HandleScope scope(this);
  Object message(&scope, runtime()->newStrFromFmtV(this, fmt, args));
  va_end(args);
  return raise(type, *message);
}

RawObject Thread::raiseWithId(LayoutId type, SymbolId msg) {
  return raise(type, runtime()->symbols()->at(msg));
}

// Convenience method for throwing a binary-operation-specific TypeError
// exception with an error message.
RawObject Thread::raiseUnsupportedBinaryOperation(
    const Handle<RawObject>& left, const Handle<RawObject>& right,
    SymbolId op_name) {
  return raiseWithFmt(LayoutId::kTypeError, "%T.%Y(%T) is not supported", &left,
                      op_name, &right);
}

void Thread::raiseBadArgument() {
  raiseWithId(LayoutId::kTypeError,
              SymbolId::kBadArgumentTypeForBuiltinOperation);
}

void Thread::raiseBadInternalCall() {
  raiseWithId(LayoutId::kSystemError, SymbolId::kBadArgumentToInternalFunction);
}

RawObject Thread::raiseMemoryError() {
  return raise(LayoutId::kMemoryError, NoneType::object());
}

RawObject Thread::raiseOSErrorFromErrno(int errno_value) {
  // TODO(matthiasb): Pick apropriate OSError subclass.
  return raiseWithFmt(LayoutId::kOSError, "[Errno %d] %s", errno_value,
                      std::strerror(errno_value));
}

RawObject Thread::raiseRequiresType(const Object& obj, SymbolId expected_type) {
  HandleScope scope(this);
  Function function(&scope, currentFrame()->function());
  Str function_name(&scope, function.name());
  return raiseWithFmt(LayoutId::kTypeError,
                      "'%S' requires a '%Y' object but got '%T'",
                      &function_name, expected_type, &obj);
}

bool Thread::hasPendingException() { return !pending_exc_type_.isNoneType(); }

bool Thread::hasPendingStopIteration() {
  if (pending_exc_type_.isType()) {
    return Type::cast(pending_exc_type_).builtinBase() ==
           LayoutId::kStopIteration;
  }
  if (runtime()->isInstanceOfType(pending_exc_type_)) {
    HandleScope scope(this);
    Type type(&scope, pending_exc_type_);
    return type.builtinBase() == LayoutId::kStopIteration;
  }
  return false;
}

bool Thread::clearPendingStopIteration() {
  if (hasPendingStopIteration()) {
    clearPendingException();
    return true;
  }
  return false;
}

RawObject Thread::pendingStopIterationValue() {
  DCHECK(hasPendingStopIteration(),
         "Shouldn't be called without a pending StopIteration");

  HandleScope scope(this);
  Object exc_value(&scope, pendingExceptionValue());
  if (runtime()->isInstanceOfStopIteration(*exc_value)) {
    StopIteration si(&scope, *exc_value);
    return si.value();
  }
  if (runtime()->isInstanceOfTuple(*exc_value)) {
    Tuple tuple(&scope, tupleUnderlying(this, exc_value));
    return tuple.at(0);
  }
  return *exc_value;
}

void Thread::ignorePendingException() {
  if (!hasPendingException()) {
    return;
  }
  fprintf(stderr, "ignore pending exception");
  if (pendingExceptionValue().isStr()) {
    RawStr message = Str::cast(pendingExceptionValue());
    word len = message.charLength();
    byte* buffer = new byte[len + 1];
    message.copyTo(buffer, len);
    buffer[len] = 0;
    fprintf(stderr, ": %s", buffer);
    delete[] buffer;
  }
  fprintf(stderr, "\n");
  clearPendingException();
  Utils::printTracebackToStderr();
}

void Thread::clearPendingException() {
  setPendingExceptionType(NoneType::object());
  setPendingExceptionValue(NoneType::object());
  setPendingExceptionTraceback(NoneType::object());
}

bool Thread::pendingExceptionMatches(LayoutId type) {
  HandleScope scope(this);
  Type exc(&scope, pendingExceptionType());
  Type parent(&scope, runtime()->typeAt(type));
  return runtime()->isSubclass(exc, parent);
}

bool Thread::hasCaughtException() {
  return !caughtExceptionType().isNoneType();
}

RawObject Thread::caughtExceptionType() {
  return ExceptionState::cast(caught_exc_stack_).type();
}

RawObject Thread::caughtExceptionValue() {
  return ExceptionState::cast(caught_exc_stack_).value();
}

RawObject Thread::caughtExceptionTraceback() {
  return ExceptionState::cast(caught_exc_stack_).traceback();
}

void Thread::setCaughtExceptionType(RawObject type) {
  ExceptionState::cast(caught_exc_stack_).setType(type);
}

void Thread::setCaughtExceptionValue(RawObject value) {
  ExceptionState::cast(caught_exc_stack_).setValue(value);
}

void Thread::setCaughtExceptionTraceback(RawObject traceback) {
  ExceptionState::cast(caught_exc_stack_).setTraceback(traceback);
}

RawObject Thread::caughtExceptionState() { return caught_exc_stack_; }

void Thread::setCaughtExceptionState(RawObject state) {
  caught_exc_stack_ = state;
}

bool Thread::isErrorValueOk(RawObject obj) {
  return (!obj.isError() && !hasPendingException()) ||
         (obj.isErrorException() && hasPendingException());
}

void Thread::visitFrames(FrameVisitor* visitor) {
  Frame* frame = currentFrame();
  while (!frame->isSentinel()) {
    if (!visitor->visit(frame)) {
      break;
    }
    frame = frame->previousFrame();
  }
}

RawObject Thread::reprEnter(const Object& obj) {
  HandleScope scope(this);
  if (api_repr_list_.isNoneType()) {
    api_repr_list_ = runtime_->newList();
  }
  List list(&scope, api_repr_list_);
  for (word i = list.numItems() - 1; i >= 0; i--) {
    if (list.at(i) == *obj) {
      return RawBool::trueObj();
    }
  }
  // TODO(emacs): When there is better error handling, raise an exception.
  runtime_->listAdd(this, list, obj);
  return RawBool::falseObj();
}

void Thread::reprLeave(const Object& obj) {
  HandleScope scope(this);
  List list(&scope, api_repr_list_);
  for (word i = list.numItems() - 1; i >= 0; i--) {
    if (list.at(i) == *obj) {
      list.atPut(i, Unbound::object());
      break;
    }
  }
}

}  // namespace python
