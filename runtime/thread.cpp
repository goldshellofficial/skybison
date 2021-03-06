// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "thread.h"

#include <signal.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "builtins-module.h"
#include "exception-builtins.h"
#include "file.h"
#include "frame.h"
#include "globals.h"
#include "handles.h"
#include "interpreter.h"
#include "module-builtins.h"
#include "objects.h"
#include "profiling.h"
#include "runtime.h"
#include "tuple-builtins.h"
#include "type-builtins.h"
#include "visitor.h"

namespace py {

void Handles::visitPointers(PointerVisitor* visitor) {
  for (Object* handle = head_; handle != nullptr;
       handle = handle->nextHandle()) {
    visitor->visitPointer(handle, PointerKind::kHandle);
  }
}

RawObject uninitializedInterpreterFunc(Thread*) {
  UNREACHABLE("interpreter main loop not initialized on this thread");
}

thread_local Thread* Thread::current_thread_ = nullptr;

Thread::Thread(Runtime* runtime, word size) : runtime_(runtime) {
  CHECK(size % kPointerSize == 0, "size must be a multiple of kPointerSize");
  start_ = new byte[size]();  // Zero-initialize the stack
  // Stack growns down in order to match machine convention
  end_ = start_ + size;
  limit_ = start_;
  stack_pointer_ = reinterpret_cast<RawObject*>(end_);
  current_frame_ = pushInitialFrame();
  setCaughtExceptionState(runtime_->newExceptionState());
}

Thread::~Thread() { delete[] start_; }

void Thread::begin() {
  Thread::setCurrentThread(this);
  runtime_->interpreter()->setupThread(this);
}

void Thread::visitRoots(PointerVisitor* visitor) {
  visitStackRoots(visitor);
  handles()->visitPointers(visitor);
  visitor->visitPointer(&api_repr_list_, PointerKind::kThread);
  visitor->visitPointer(&asyncgen_hooks_finalizer_, PointerKind::kThread);
  visitor->visitPointer(&asyncgen_hooks_first_iter_, PointerKind::kThread);
  visitor->visitPointer(&caught_exc_stack_, PointerKind::kThread);
  visitor->visitPointer(&contextvars_context_, PointerKind::kThread);
  visitor->visitPointer(&pending_exc_traceback_, PointerKind::kThread);
  visitor->visitPointer(&pending_exc_type_, PointerKind::kThread);
  visitor->visitPointer(&pending_exc_value_, PointerKind::kThread);
  visitor->visitPointer(&profiling_data_, PointerKind::kThread);
  visitor->visitPointer(&str_offset_str_, PointerKind::kThread);
}

void Thread::visitStackRoots(PointerVisitor* visitor) {
  auto address = reinterpret_cast<uword>(stackPointer());
  auto end = reinterpret_cast<uword>(end_);
  std::memset(start_, 0, reinterpret_cast<byte*>(address) - start_);
  for (; address < end; address += kPointerSize) {
    visitor->visitPointer(reinterpret_cast<RawObject*>(address),
                          PointerKind::kStack);
  }
}

Thread* Thread::current() { return Thread::current_thread_; }

bool Thread::isMainThread() { return this == runtime_->mainThread(); }

namespace {

class UserVisibleFrameVisitor : public FrameVisitor {
 public:
  UserVisibleFrameVisitor(Thread* thread, word depth)
      : thread_(thread),
        scope_(thread),
        result_(&scope_, NoneType::object()),
        heap_frame_(&scope_, NoneType::object()),
        next_heap_frame_(&scope_, NoneType::object()),
        depth_(depth) {}

  bool visit(Frame* frame) {
    if (isHiddenFrame(frame)) {
      return true;
    }
    if (call_ < depth_) {
      call_++;
      return true;
    }
    // Once visitor reaches the target depth, start creating a linked list of
    // FrameProxys objects.
    // TOOD(T63960421): Cache an already created object in the stack frame.
    Function function(&scope_, frame->function());
    Object lasti(&scope_, NoneType::object());
    if (!frame->isNative()) {
      lasti = SmallInt::fromWord(frame->virtualPC());
    }
    heap_frame_ = thread_->runtime()->newFrameProxy(thread_, function, lasti);
    // TODO(T89882231) unconditionally add frameLocals. We cannot currently do
    // this because uninitialized variables are not cleared and will read
    // arbitrary values.
    if (function.hasOptimizedOrNewlocals()) {
      FrameProxy::cast(*heap_frame_).setLocals(thread_->runtime()->newDict());
    } else {
      FrameProxy::cast(*heap_frame_).setLocals(frameLocals(thread_, frame));
    }
    if (result_.isNoneType()) {
      // The head of the linked list is returned as the result.
      result_ = *heap_frame_;
    } else {
      FrameProxy::cast(*next_heap_frame_).setBack(*heap_frame_);
    }
    next_heap_frame_ = *heap_frame_;
    return true;
  }

  RawObject result() { return *result_; }

 private:
  bool isHiddenFrame(Frame* frame) {
    if (frame == nullptr || frame->isSentinel()) {
      return true;
    }
    RawFunction function = Function::cast(frame->function());
    word builtins_module_id = thread_->runtime()->builtinsModuleId();
    // PyCFunction do not generate a frame in cpython and therefore do
    // not show up in sys._getframe(). Our builtin functions do create a
    // frame so we hide frames of functions in builtins from the user.
    // TODO(T64005113): This logic should be applied to each function.
    if (function.moduleObject().isModule() &&
        Module::cast(function.moduleObject()).id() == builtins_module_id) {
      return true;
    }
    return false;
  }

  Thread* thread_;
  HandleScope scope_;
  Object result_;
  Object heap_frame_;
  Object next_heap_frame_;

  word call_ = 0;
  word depth_;

  DISALLOW_HEAP_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(UserVisibleFrameVisitor);
};

}  // namespace

RawObject Thread::heapFrameAtDepth(word depth) {
  UserVisibleFrameVisitor visitor(this, depth);
  visitFrames(&visitor);
  return visitor.result();
}

void Thread::setCurrentThread(Thread* thread) {
  Thread::current_thread_ = thread;
}

void Thread::clearInterrupt(InterruptKind kind) {
  interrupt_flags_ &= ~kind;
  if (interrupt_flags_ == 0) {
    limit_ = start_;
  }
}

void Thread::interrupt(InterruptKind kind) {
  interrupt_flags_ |= kind;
  limit_ = end_;
}

bool Thread::handleInterrupt(word size) {
  // Is it a real stack overflow?
  if (reinterpret_cast<byte*>(stackPointer()) - size < start_) {
    raiseWithFmt(LayoutId::kRecursionError, "maximum recursion depth exceeded");
    return true;
  }
  uint8_t interrupt_flags = interrupt_flags_;
  if ((interrupt_flags & kSignal) != 0 &&
      !runtime_->handlePendingSignals(this).isNoneType()) {
    return true;
  }
  if (interrupt_flags & kReinitInterpreter) {
    clearInterrupt(kReinitInterpreter);
    runtime_->interpreter()->setupThread(this);
  }
  return false;
}

void Thread::handleInterruptWithFrame() {
  if ((interrupt_flags_ & kProfile) != 0) {
    profiling_call(this);
  }
}

void Thread::linkFrame(Frame* frame) {
  frame->setPreviousFrame(current_frame_);
  current_frame_ = frame;
}

inline Frame* Thread::openAndLinkFrame(word size, word locals_offset) {
  // Initialize the frame.
  byte* new_sp = reinterpret_cast<byte*>(stack_pointer_) - size;
  stack_pointer_ = reinterpret_cast<RawObject*>(new_sp);
  Frame* frame = reinterpret_cast<Frame*>(new_sp);
  frame->setLocalsOffset(locals_offset);

  // return a pointer to the base of the frame
  linkFrame(frame);
  DCHECK(frame->isInvalid() == nullptr, "invalid frame");
  return frame;
}

ALWAYS_INLINE Frame* Thread::pushNativeFrameImpl(word locals_offset) {
  return openAndLinkFrame(Frame::kSize, locals_offset);
}

NEVER_INLINE Frame* Thread::handleInterruptPushNativeFrame(word locals_offset) {
  if (handleInterrupt(Frame::kSize)) {
    return nullptr;
  }
  Frame* result = pushNativeFrameImpl(locals_offset);
  handleInterruptWithFrame();
  return result;
}

Frame* Thread::pushNativeFrame(word nargs) {
  word locals_offset = Frame::kSize + nargs * kPointerSize;
  if (UNLIKELY(wouldStackOverflow(Frame::kSize))) {
    return handleInterruptPushNativeFrame(locals_offset);
  }
  return pushNativeFrameImpl(locals_offset);
}

ALWAYS_INLINE Frame* Thread::pushCallFrameImpl(RawFunction function,
                                               word stack_size,
                                               word locals_offset) {
  Frame* result = openAndLinkFrame(stack_size, locals_offset);
  result->setBytecode(MutableBytes::cast(function.rewrittenBytecode()));
  result->setCaches(function.caches());
  result->setVirtualPC(0);
  result->setBlockStackDepthReturnMode(0);
  return result;
}

NEVER_INLINE Frame* Thread::handleInterruptPushCallFrame(
    RawFunction function, word max_stack_size, word initial_stack_size,
    word locals_offset) {
  if (handleInterrupt(max_stack_size)) {
    return nullptr;
  }
  Frame* result =
      pushCallFrameImpl(function, initial_stack_size, locals_offset);
  handleInterruptWithFrame();
  return result;
}

Frame* Thread::pushCallFrame(RawFunction function) {
  word initial_stack_size = Frame::kSize + function.totalVars() * kPointerSize;
  word stack_size = SmallInt::cast(function.stacksizeOrBuiltin()).value();
  word max_stack_size = initial_stack_size + stack_size * kPointerSize;
  word locals_offset = initial_stack_size + function.totalArgs() * kPointerSize;
  if (UNLIKELY(wouldStackOverflow(max_stack_size))) {
    return handleInterruptPushCallFrame(function, max_stack_size,
                                        initial_stack_size, locals_offset);
  }
  return pushCallFrameImpl(function, initial_stack_size, locals_offset);
}

ALWAYS_INLINE Frame* Thread::pushGeneratorFrameImpl(
    const GeneratorFrame& generator_frame, word size) {
  word max_stack_size = generator_frame.maxStackSize();
  word stack_size = generator_frame.stackSize();
  word unused_stack = max_stack_size - stack_size;
  size -= unused_stack * kPointerSize;
  byte* src = reinterpret_cast<byte*>(generator_frame.address() +
                                      RawGeneratorFrame::kFrameOffset +
                                      unused_stack * kPointerSize);
  byte* dest = reinterpret_cast<byte*>(stack_pointer_) - size;
  std::memcpy(dest, src, size);
  Frame* result = reinterpret_cast<Frame*>(dest + stack_size * kPointerSize);
  setStackPointer(reinterpret_cast<RawObject*>(dest));
  linkFrame(result);
  DCHECK(result->isInvalid() == nullptr, "invalid frame");
  return result;
}

NEVER_INLINE Frame* Thread::handleInterruptPushGeneratorFrame(
    const GeneratorFrame& generator_frame, word size) {
  if (handleInterrupt(size)) {
    return nullptr;
  }
  Frame* result = pushGeneratorFrameImpl(generator_frame, size);
  handleInterruptWithFrame();
  return result;
}

Frame* Thread::pushGeneratorFrame(const GeneratorFrame& generator_frame) {
  word num_frame_words = generator_frame.numFrameWords();
  word size = num_frame_words * kPointerSize;
  if (UNLIKELY(wouldStackOverflow(size))) {
    return handleInterruptPushGeneratorFrame(generator_frame, size);
  }
  return pushGeneratorFrameImpl(generator_frame, size);
}

Frame* Thread::pushInitialFrame() {
  byte* sp = end_ - Frame::kSize;
  CHECK(sp > start_, "no space for initial frame");
  Frame* frame = reinterpret_cast<Frame*>(sp);
  frame->setLocalsOffset(Frame::kSize);
  stack_pointer_ = reinterpret_cast<RawObject*>(sp);
  frame->setPreviousFrame(nullptr);
  return frame;
}

Frame* Thread::popFrameToGeneratorFrame(const GeneratorFrame& generator_frame) {
  word max_stack_size = generator_frame.maxStackSize();
  word stack_size = valueStackSize();
  word unused_stack = max_stack_size - stack_size;
  DCHECK(stack_size <= max_stack_size,
         "not enough space in RawGeneratorBase to save live stack");
  byte* dest = reinterpret_cast<byte*>(generator_frame.address() +
                                       RawGeneratorFrame::kFrameOffset +
                                       unused_stack * kPointerSize);
  byte* src = reinterpret_cast<byte*>(valueStackBase() - stack_size);
  word copy_size =
      (generator_frame.numFrameWords() - unused_stack) * kPointerSize;
  std::memcpy(dest, src, copy_size);
  generator_frame.setStackSize(stack_size);
  return popFrame();
}

RawObject Thread::exec(const Code& code, const Module& module,
                       const Object& implicit_globals) {
  HandleScope scope(this);
  Object qualname(&scope, code.name());

  if (code.hasOptimizedOrNewlocals()) {
    UNIMPLEMENTED("exec() on code with CO_OPTIMIZED / CO_NEWLOCALS");
  }

  Runtime* runtime = this->runtime();
  Object builtins_module_obj(&scope,
                             moduleAtById(this, module, ID(__builtins__)));
  if (builtins_module_obj.isErrorNotFound()) {
    Module builtins(&scope, runtime->findModuleById(ID(builtins)));
    DCHECK(!builtins.isErrorNotFound(), "invalid builtins module");
    Object proxy(&scope, builtins.moduleProxy());
    moduleAtPutById(this, module, ID(__builtins__), proxy);
  }
  Function function(&scope,
                    runtime->newFunctionWithCode(this, qualname, code, module));
  return callFunctionWithImplicitGlobals(function, implicit_globals);
}

RawObject Thread::callFunctionWithImplicitGlobals(
    const Function& function, const Object& implicit_globals) {
  CHECK(!function.hasOptimizedOrNewlocals(),
        "function must not have CO_OPTIMIZED or CO_NEWLOCALS");

  // Push implicit globals and function.
  stackPush(*implicit_globals);
  stackPush(*function);
  Frame* frame = pushCallFrame(*function);
  if (frame == nullptr) {
    return Error::exception();
  }
  if (function.hasFreevarsOrCellvars()) {
    processFreevarsAndCellvars(this, frame);
  }
  RawObject result = Interpreter::execute(this);
  DCHECK(stackTop() == *implicit_globals, "stack mismatch");
  stackDrop(1);
  return result;
}

RawObject Thread::invokeMethod1(const Object& receiver, SymbolId selector) {
  HandleScope scope(this);
  Object method(&scope, Interpreter::lookupMethod(this, receiver, selector));
  if (method.isError()) return *method;
  return Interpreter::callMethod1(this, method, receiver);
}

RawObject Thread::invokeMethod2(const Object& receiver, SymbolId selector,
                                const Object& arg1) {
  HandleScope scope(this);
  Object method(&scope, Interpreter::lookupMethod(this, receiver, selector));
  if (method.isError()) return *method;
  return Interpreter::callMethod2(this, method, receiver, arg1);
}

RawObject Thread::invokeMethod3(const Object& receiver, SymbolId selector,
                                const Object& arg1, const Object& arg2) {
  HandleScope scope(this);
  Object method(&scope, Interpreter::lookupMethod(this, receiver, selector));
  if (method.isError()) return *method;
  return Interpreter::callMethod3(this, method, receiver, arg1, arg2);
}

RawObject Thread::invokeMethodStatic1(LayoutId type, SymbolId method_name,
                                      const Object& receiver) {
  HandleScope scope(this);
  Object type_obj(&scope, runtime()->typeAt(type));
  if (type_obj.isError()) return *type_obj;
  Type type_handle(&scope, *type_obj);
  Object method(&scope, typeLookupInMroById(this, *type_handle, method_name));
  if (method.isError()) return *method;
  return Interpreter::callMethod1(this, method, receiver);
}

RawObject Thread::invokeMethodStatic2(LayoutId type, SymbolId method_name,
                                      const Object& receiver,
                                      const Object& arg1) {
  HandleScope scope(this);
  Object type_obj(&scope, runtime()->typeAt(type));
  if (type_obj.isError()) return *type_obj;
  Type type_handle(&scope, *type_obj);
  Object method(&scope, typeLookupInMroById(this, *type_handle, method_name));
  if (method.isError()) return *method;
  return Interpreter::callMethod2(this, method, receiver, arg1);
}

RawObject Thread::invokeMethodStatic3(LayoutId type, SymbolId method_name,
                                      const Object& receiver,
                                      const Object& arg1, const Object& arg2) {
  HandleScope scope(this);
  Object type_obj(&scope, runtime()->typeAt(type));
  if (type_obj.isError()) return *type_obj;
  Type type_handle(&scope, *type_obj);
  Object method(&scope, typeLookupInMroById(this, *type_handle, method_name));
  if (method.isError()) return *method;
  return Interpreter::callMethod3(this, method, receiver, arg1, arg2);
}

RawObject Thread::invokeMethodStatic4(LayoutId type, SymbolId method_name,
                                      const Object& receiver,
                                      const Object& arg1, const Object& arg2,
                                      const Object& arg3) {
  HandleScope scope(this);
  Object type_obj(&scope, runtime()->typeAt(type));
  if (type_obj.isError()) return *type_obj;
  Type type_handle(&scope, *type_obj);
  Object method(&scope, typeLookupInMroById(this, *type_handle, method_name));
  if (method.isError()) return *method;
  return Interpreter::callMethod4(this, method, receiver, arg1, arg2, arg3);
}

RawObject Thread::invokeFunction0(SymbolId module, SymbolId name) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::call0(this, func);
}

RawObject Thread::invokeFunction1(SymbolId module, SymbolId name,
                                  const Object& arg1) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::call1(this, func, arg1);
}

RawObject Thread::invokeFunction2(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::call2(this, func, arg1, arg2);
}

RawObject Thread::invokeFunction3(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2,
                                  const Object& arg3) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::call3(this, func, arg1, arg2, arg3);
}

RawObject Thread::invokeFunction4(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2,
                                  const Object& arg3, const Object& arg4) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::call4(this, func, arg1, arg2, arg3, arg4);
}

RawObject Thread::invokeFunction5(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2,
                                  const Object& arg3, const Object& arg4,
                                  const Object& arg5) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::call5(this, func, arg1, arg2, arg3, arg4, arg5);
}

RawObject Thread::invokeFunction6(SymbolId module, SymbolId name,
                                  const Object& arg1, const Object& arg2,
                                  const Object& arg3, const Object& arg4,
                                  const Object& arg5, const Object& arg6) {
  HandleScope scope(this);
  Object func(&scope, runtime()->lookupNameInModule(this, module, name));
  if (func.isError()) return *func;
  return Interpreter::call6(this, func, arg1, arg2, arg3, arg4, arg5, arg6);
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
  // If raise is called with an exception instance use the original traceback
  if (runtime()->isInstanceOfBaseException(*value_obj)) {
    traceback_obj = value_obj.rawCast<RawBaseException>().traceback();
  }

  value_obj = chainExceptionContext(type_obj, value_obj);
  if (value_obj.isErrorException()) return Error::exception();

  setPendingExceptionType(*type_obj);
  setPendingExceptionValue(*value_obj);
  setPendingExceptionTraceback(*traceback_obj);
  return Error::exception();
}

RawObject Thread::chainExceptionContext(const Type& type, const Object& value) {
  HandleScope scope(this);
  Object caught_exc_state_obj(&scope, topmostCaughtExceptionState());
  if (caught_exc_state_obj.isNoneType()) {
    return *value;
  }
  ExceptionState caught_exc_state(&scope, *caught_exc_state_obj);

  Object fixed_value(&scope, *value);
  if (!runtime()->isInstanceOfBaseException(*value)) {
    // Perform partial normalization before attempting to set __context__.
    fixed_value = createException(this, type, value);
    if (fixed_value.isError()) return *fixed_value;
  }

  // Avoid creating cycles by breaking any link from caught_value to value
  // before setting value's __context__.
  BaseException caught_value(&scope, caught_exc_state.value());
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

RawObject Thread::raiseWithFmtChainingPendingAsCause(LayoutId type,
                                                     const char* fmt, ...) {
  HandleScope scope(this);

  va_list args;
  va_start(args, fmt);
  Object message(&scope, runtime()->newStrFromFmtV(this, fmt, args));
  va_end(args);

  Object pending_type(&scope, pendingExceptionType());
  Object pending_value(&scope, pendingExceptionValue());
  Object pending_traceback(&scope, pendingExceptionTraceback());
  clearPendingException();
  normalizeException(this, &pending_type, &pending_value, &pending_traceback);

  Type new_exc_type(&scope, runtime()->typeAt(type));
  BaseException new_exc(&scope, createException(this, new_exc_type, message));
  new_exc.setCause(*pending_value);
  new_exc.setContext(*pending_value);
  setPendingExceptionType(*new_exc_type);
  setPendingExceptionValue(*new_exc);
  setPendingExceptionTraceback(NoneType::object());
  return Error::exception();
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
  raiseWithFmt(LayoutId::kTypeError,
               "bad argument type for built-in operation");
}

RawObject Thread::raiseBadInternalCall() {
  return raiseWithFmt(LayoutId::kSystemError,
                      "bad argument to internal function");
}

RawObject Thread::raiseMemoryError() {
  return raise(LayoutId::kMemoryError, NoneType::object());
}

RawObject Thread::raiseOSErrorFromErrno(int errno_value) {
  HandleScope scope(this);
  Object type(&scope, runtime()->typeAt(LayoutId::kOSError));
  NoneType none(&scope, NoneType::object());
  return raiseFromErrnoWithFilenames(type, errno_value, none, none);
}

RawObject Thread::raiseFromErrnoWithFilenames(const Object& type,
                                              int errno_value,
                                              const Object& filename0,
                                              const Object& filename1) {
  HandleScope scope(this);
  if (errno_value == EINTR) {
    Object result(&scope, runtime_->handlePendingSignals(this));
    if (result.isErrorException()) return *result;
  }

  stackPush(*type);
  stackPush(SmallInt::fromWord(errno_value));
  stackPush(errno_value == 0
                ? runtime_->symbols()->at(ID(Error))
                : Runtime::internStrFromCStr(this, std::strerror(errno_value)));
  word nargs = 2;
  if (!filename0.isNoneType()) {
    stackPush(*filename0);
    ++nargs;
    if (!filename1.isNoneType()) {
      stackPush(SmallInt::fromWord(0));
      stackPush(*filename1);
      nargs += 2;
    }
  } else {
    DCHECK(filename1.isNoneType(), "expected filename1 to be None");
  }
  Object exception(&scope, Interpreter::call(this, nargs));
  if (exception.isErrorException()) return *exception;
  return raiseWithType(runtime_->typeOf(*exception), *exception);
}

RawObject Thread::raiseRequiresType(const Object& obj, SymbolId expected_type) {
  HandleScope scope(this);
  Function function(&scope, currentFrame()->function());
  Str function_name(&scope, function.name());
  return raiseWithFmt(LayoutId::kTypeError,
                      "'%S' for '%Y' objects doesn't apply to a '%T' object",
                      &function_name, expected_type, &obj);
}

RawObject Thread::raiseStopAsyncIteration() {
  return raise(LayoutId::kStopAsyncIteration, NoneType::object());
}

RawObject Thread::raiseStopIteration() {
  return raise(LayoutId::kStopIteration, NoneType::object());
}

RawObject Thread::raiseStopIterationWithValue(const Object& value) {
  if (runtime()->isInstanceOfTuple(*value) ||
      runtime()->isInstanceOfBaseException(*value)) {
    // TODO(T67598788): Remove this special case. For now this works around
    // the behavior of normalizeException() when it's called in
    // Interpreter::unwind() as part of returning values from generators. Our
    // desired end-state is StopIterations will be treated as a special
    // optimized path which, among other properties, are not processed by
    // normalization.
    HandleScope scope(this);
    Layout layout(&scope, runtime()->layoutAt(LayoutId::kStopIteration));
    StopIteration stop_iteration(&scope, runtime()->newInstance(layout));
    stop_iteration.setArgs(runtime()->newTupleWith1(value));
    stop_iteration.setValue(*value);
    stop_iteration.setCause(Unbound::object());
    stop_iteration.setContext(Unbound::object());
    stop_iteration.setTraceback(Unbound::object());
    stop_iteration.setSuppressContext(RawBool::falseObj());
    return raise(LayoutId::kStopIteration, *stop_iteration);
  }
  return raise(LayoutId::kStopIteration, *value);
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
    return tupleUnderlying(*exc_value).at(0);
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
    word len = message.length();
    byte* buffer = new byte[len + 1];
    message.copyTo(buffer, len);
    buffer[len] = 0;
    fprintf(stderr, ": %s", buffer);
    delete[] buffer;
  }
  fprintf(stderr, "\n");
  clearPendingException();
  runtime_->printTraceback(this, File::kStderr);
}

void Thread::clearPendingException() {
  setPendingExceptionType(NoneType::object());
  setPendingExceptionValue(NoneType::object());
  setPendingExceptionTraceback(NoneType::object());
}

bool Thread::pendingExceptionMatches(LayoutId type) {
  return typeIsSubclass(pendingExceptionType(), runtime()->typeAt(type));
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

RawObject Thread::topmostCaughtExceptionState() {
  HandleScope scope(this);
  Object exc_state(&scope, caught_exc_stack_);
  while (!exc_state.isNoneType() &&
         ExceptionState::cast(*exc_state).type().isNoneType()) {
    exc_state = ExceptionState::cast(*exc_state).previous();
  }
  return *exc_state;
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

void Thread::enableProfiling() { interrupt(kProfile); }

void Thread::disableProfiling() { clearInterrupt(kProfile); }

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

word Thread::strOffset(const Str& str, word index) {
  if (str != str_offset_str_) {
    str_offset_str_ = *str;
    str_offset_index_ = index;
    str_offset_offset_ = str.offsetByCodePoints(0, index);
    return str_offset_offset_;
  }
  word index_diff = index - str_offset_index_;
  word offset = str.offsetByCodePoints(str_offset_offset_, index_diff);
  if (0 <= offset && offset < str.length()) {
    str_offset_index_ = index;
    str_offset_offset_ = offset;
  }
  return offset;
}

}  // namespace py
