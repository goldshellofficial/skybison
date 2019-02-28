#include "interpreter.h"

#include <cstdio>
#include <cstdlib>

#include "builtins-module.h"
#include "dict-builtins.h"
#include "exception-builtins.h"
#include "frame.h"
#include "list-builtins.h"
#include "objects.h"
#include "runtime.h"
#include "str-builtins.h"
#include "thread.h"
#include "trampolines.h"
#include "tuple-builtins.h"

namespace python {

RawObject Interpreter::prepareCallableCall(Thread* thread, Frame* frame,
                                           word callable_idx, word* nargs) {
  HandleScope scope(thread);
  Object callable(&scope, frame->peek(callable_idx));
  DCHECK(!callable.isFunction(),
         "prepareCallableCall should only be called on non-function types");
  bool is_bound;
  if (callable.isBoundMethod()) {
    // Shift all arguments on the stack down by 1 and unpack the BoundMethod.
    //
    // We don't need to worry too much about the performance overhead for method
    // calls here.
    //
    // Python 3.7 introduces two new opcodes, LOAD_METHOD and CALL_METHOD, that
    // eliminate the need to create a temporary BoundMethod object when
    // performing a method call.
    //
    // The other pattern of bound method usage occurs when someone passes around
    // a reference to a method e.g.:
    //
    //   m = foo.method
    //   m()
    //
    // Our contention is that uses of this pattern are not performance
    // sensitive.
    BoundMethod method(&scope, *callable);
    frame->insertValueAt(method.self(), callable_idx);
    is_bound = false;
    callable = method.function();
  } else {
    Runtime* runtime = thread->runtime();
    for (;;) {
      Type type(&scope, runtime->typeOf(*callable));
      Object attr(&scope, runtime->lookupSymbolInMro(thread, type,
                                                     SymbolId::kDunderCall));
      if (!attr.isError()) {
        if (attr.isFunction()) {
          // Do not create a short-lived bound method object.
          is_bound = false;
          callable = *attr;
          break;
        }
        if (runtime->isNonDataDescriptor(thread, attr)) {
          Object owner(&scope, *type);
          callable = callDescriptorGet(thread, frame, attr, callable, owner);
          if (callable.isFunction()) {
            // Descriptors do not return unbound methods.
            is_bound = true;
            break;
          }
          // Retry the lookup using the object returned by the descriptor.
          continue;
        }
      }
      return thread->raiseTypeErrorWithCStr("object is not callable");
    }
  }
  if (is_bound) {
    frame->setValueAt(*callable, callable_idx);
  } else {
    frame->insertValueAt(*callable, callable_idx + 1);
    *nargs += 1;
  }
  return *callable;
}

RawObject Interpreter::call(Thread* thread, Frame* frame, word nargs) {
  RawObject* sp = frame->valueStackTop() + nargs + 1;
  RawObject callable = frame->peek(nargs);
  if (!callable->isFunction()) {
    callable = prepareCallableCall(thread, frame, nargs, &nargs);
  }
  if (callable->isError()) {
    frame->setValueStackTop(sp);
    return callable;
  }
  RawObject result = RawFunction::cast(callable)->entry()(thread, frame, nargs);
  // Clear the stack of the function object and return.
  frame->setValueStackTop(sp);
  return result;
}

RawObject Interpreter::callKw(Thread* thread, Frame* frame, word nargs) {
  // Top of stack is a tuple of keyword argument names in the order they
  // appear on the stack.
  RawObject* sp = frame->valueStackTop() + nargs + 2;
  RawObject result;
  RawObject callable = frame->peek(nargs + 1);
  if (callable->isFunction()) {
    result = RawFunction::cast(callable)->entryKw()(thread, frame, nargs);
  } else {
    callable = prepareCallableCall(thread, frame, nargs + 1, &nargs);
    result = RawFunction::cast(callable)->entryKw()(thread, frame, nargs);
  }
  frame->setValueStackTop(sp);
  return result;
}

RawObject Interpreter::callEx(Thread* thread, Frame* frame, word flags) {
  // Low bit of flags indicates whether var-keyword argument is on TOS.
  // In all cases, var-positional tuple is next, followed by the function
  // pointer.
  HandleScope scope(thread);
  word function_position = (flags & CallFunctionExFlag::VAR_KEYWORDS) ? 2 : 1;
  RawObject* sp = frame->valueStackTop() + function_position + 1;
  Object callable(&scope, frame->peek(function_position));
  word args_position = function_position - 1;
  Object args_obj(&scope, frame->peek(args_position));
  if (!args_obj.isTuple()) {
    // Make sure the argument sequence is a tuple.
    args_obj = sequenceAsTuple(thread, args_obj);
    if (args_obj.isError()) {
      frame->setValueStackTop(sp);
      return *args_obj;
    }
    frame->setValueAt(*args_obj, args_position);
  }
  if (!callable.isFunction()) {
    // Create a new argument tuple with self as the first argument
    Tuple args(&scope, *args_obj);
    Tuple new_args(&scope, thread->runtime()->newTuple(args.length() + 1));
    Object target(&scope, NoneType::object());
    if (callable.isBoundMethod()) {
      BoundMethod method(&scope, *callable);
      new_args.atPut(0, method.self());
      target = method.function();
    } else {
      new_args.atPut(0, *callable);
      target = lookupMethod(thread, frame, callable, SymbolId::kDunderCall);
    }
    new_args.replaceFromWith(1, *args);
    frame->setValueAt(*target, function_position);
    frame->setValueAt(*new_args, args_position);
    callable = *target;
  }
  RawObject result =
      RawFunction::cast(*callable)->entryEx()(thread, frame, flags);
  frame->setValueStackTop(sp);
  return result;
}

RawObject Interpreter::stringJoin(Thread* thread, RawObject* sp, word num) {
  word new_len = 0;
  for (word i = num - 1; i >= 0; i--) {
    if (!sp[i]->isStr()) {
      UNIMPLEMENTED("Conversion of non-string values not supported.");
    }
    new_len += RawStr::cast(sp[i])->length();
  }

  if (new_len <= RawSmallStr::kMaxLength) {
    byte buffer[RawSmallStr::kMaxLength];
    byte* ptr = buffer;
    for (word i = num - 1; i >= 0; i--) {
      RawStr str = RawStr::cast(sp[i]);
      word len = str->length();
      str->copyTo(ptr, len);
      ptr += len;
    }
    return SmallStr::fromBytes(View<byte>(buffer, new_len));
  }

  HandleScope scope(thread);
  LargeStr result(&scope, thread->runtime()->heap()->createLargeStr(new_len));
  word offset = RawLargeStr::kDataOffset;
  for (word i = num - 1; i >= 0; i--) {
    RawStr str = RawStr::cast(sp[i]);
    word len = str->length();
    str->copyTo(reinterpret_cast<byte*>(result.address() + offset), len);
    offset += len;
  }
  return *result;
}

RawObject Interpreter::callDescriptorGet(Thread* thread, Frame* caller,
                                         const Object& descriptor,
                                         const Object& receiver,
                                         const Object& receiver_type) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Type descriptor_type(&scope, runtime->typeOf(*descriptor));
  Object method(&scope, runtime->lookupSymbolInMro(thread, descriptor_type,
                                                   SymbolId::kDunderGet));
  DCHECK(!method.isError(), "no __get__ method found");
  return callMethod3(thread, caller, method, descriptor, receiver,
                     receiver_type);
}

RawObject Interpreter::callDescriptorSet(Thread* thread, Frame* caller,
                                         const Object& descriptor,
                                         const Object& receiver,
                                         const Object& value) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Type descriptor_type(&scope, runtime->typeOf(*descriptor));
  Object method(&scope, runtime->lookupSymbolInMro(thread, descriptor_type,
                                                   SymbolId::kDunderSet));
  DCHECK(!method.isError(), "no __set__ method found");
  return callMethod3(thread, caller, method, descriptor, receiver, value);
}

RawObject Interpreter::callDescriptorDelete(Thread* thread, Frame* caller,
                                            const Object& descriptor,
                                            const Object& receiver) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Type descriptor_type(&scope, runtime->typeOf(*descriptor));
  Object method(&scope, runtime->lookupSymbolInMro(thread, descriptor_type,
                                                   SymbolId::kDunderDelete));
  DCHECK(!method.isError(), "no __delete__ method found");
  return callMethod2(thread, caller, method, descriptor, receiver);
}

RawObject Interpreter::lookupMethod(Thread* thread, Frame* caller,
                                    const Object& receiver, SymbolId selector) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Type type(&scope, runtime->typeOf(*receiver));
  Object method(&scope, runtime->lookupSymbolInMro(thread, type, selector));
  if (method.isFunction()) {
    // Do not create a short-lived bound method object.
    return *method;
  }
  if (!method.isError()) {
    if (runtime->isNonDataDescriptor(thread, method)) {
      Object owner(&scope, *type);
      return callDescriptorGet(thread, caller, method, receiver, owner);
    }
  }
  return *method;
}

RawObject Interpreter::callFunction0(Thread* thread, Frame* caller,
                                     const Object& func) {
  caller->pushValue(*func);
  return call(thread, caller, 0);
}

RawObject Interpreter::callFunction1(Thread* thread, Frame* caller,
                                     const Object& func, const Object& arg1) {
  caller->pushValue(*func);
  caller->pushValue(*arg1);
  return call(thread, caller, 1);
}

RawObject Interpreter::callFunction2(Thread* thread, Frame* caller,
                                     const Object& func, const Object& arg1,
                                     const Object& arg2) {
  caller->pushValue(*func);
  caller->pushValue(*arg1);
  caller->pushValue(*arg2);
  return call(thread, caller, 2);
}

RawObject Interpreter::callFunction3(Thread* thread, Frame* caller,
                                     const Object& func, const Object& arg1,
                                     const Object& arg2, const Object& arg3) {
  caller->pushValue(*func);
  caller->pushValue(*arg1);
  caller->pushValue(*arg2);
  caller->pushValue(*arg3);
  return call(thread, caller, 3);
}

RawObject Interpreter::callFunction(Thread* thread, Frame* caller,
                                    const Object& func, const Tuple& args) {
  caller->pushValue(*func);
  word length = args.length();
  for (word i = 0; i < length; i++) {
    caller->pushValue(args.at(i));
  }
  return call(thread, caller, length);
}

RawObject Interpreter::callMethod1(Thread* thread, Frame* caller,
                                   const Object& method, const Object& self) {
  word nargs = 0;
  caller->pushValue(*method);
  if (method.isFunction()) {
    caller->pushValue(*self);
    nargs += 1;
  }
  return call(thread, caller, nargs);
}

RawObject Interpreter::callMethod2(Thread* thread, Frame* caller,
                                   const Object& method, const Object& self,
                                   const Object& other) {
  word nargs = 1;
  caller->pushValue(*method);
  if (method.isFunction()) {
    caller->pushValue(*self);
    nargs += 1;
  }
  caller->pushValue(*other);
  return call(thread, caller, nargs);
}

RawObject Interpreter::callMethod3(Thread* thread, Frame* caller,
                                   const Object& method, const Object& self,
                                   const Object& arg1, const Object& arg2) {
  word nargs = 2;
  caller->pushValue(*method);
  if (method.isFunction()) {
    caller->pushValue(*self);
    nargs += 1;
  }
  caller->pushValue(*arg1);
  caller->pushValue(*arg2);
  return call(thread, caller, nargs);
}

RawObject Interpreter::callMethod4(Thread* thread, Frame* caller,
                                   const Object& method, const Object& self,
                                   const Object& arg1, const Object& arg2,
                                   const Object& arg3) {
  word nargs = 3;
  caller->pushValue(*method);
  if (method.isFunction()) {
    caller->pushValue(*self);
    nargs += 1;
  }
  caller->pushValue(*arg1);
  caller->pushValue(*arg2);
  caller->pushValue(*arg3);
  return call(thread, caller, nargs);
}

static RawObject raiseUnaryOpTypeError(Thread* thread, const Object& object,
                                       SymbolId selector) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Str type_name(&scope, Type::cast(runtime->typeOf(*object))->name());
  // TODO(T41091163) Having a format character for Str objects would be nice.
  unique_c_ptr<char> type_name_cstr(type_name.toCStr());
  const char* op_name = runtime->symbols()->literalAt(selector);
  return thread->raiseTypeError(
      runtime->newStrFromFormat("bad operand type for unary '%s': '%.200s'",
                                op_name, type_name_cstr.get()));
}

RawObject Interpreter::unaryOperation(Thread* thread, const Object& self,
                                      SymbolId selector) {
  RawObject result = thread->invokeMethod1(self, selector);
  if (result.isError() && !thread->hasPendingException()) {
    return raiseUnaryOpTypeError(thread, self, selector);
  }
  return result;
}

bool Interpreter::doUnaryOperation(SymbolId selector, Context* ctx) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Object receiver(&scope, ctx->frame->topValue());
  RawObject result = unaryOperation(thread, receiver, selector);
  if (result.isError()) return unwind(ctx);
  ctx->frame->setTopValue(result);
  return false;
}

RawObject Interpreter::binaryOperation(Thread* thread, Frame* caller,
                                       BinaryOp op, const Object& self,
                                       const Object& other) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();

  SymbolId selector = runtime->binaryOperationSelector(op);
  Object self_method(&scope, lookupMethod(thread, caller, self, selector));

  SymbolId swapped_selector = runtime->swappedBinaryOperationSelector(op);
  Object self_reversed_method(
      &scope, lookupMethod(thread, caller, self, swapped_selector));
  Object other_reversed_method(
      &scope, lookupMethod(thread, caller, other, swapped_selector));

  bool try_other = true;
  if (!self_method.isError()) {
    if (runtime->shouldReverseBinaryOperation(
            thread, self, self_reversed_method, other, other_reversed_method)) {
      Object result(&scope, callMethod2(thread, caller, other_reversed_method,
                                        other, self));
      if (!result.isNotImplemented()) return *result;
      try_other = false;
    }
    Object result(&scope,
                  callMethod2(thread, caller, self_method, self, other));
    if (!result.isNotImplemented()) return *result;
  }
  if (try_other && !other_reversed_method.isError()) {
    Object result(&scope, callMethod2(thread, caller, other_reversed_method,
                                      other, self));
    if (!result.isNotImplemented()) return *result;
  }

  Str op_name(&scope,
              runtime->symbols()->at(runtime->binaryOperationSelector(op)));
  return thread->raiseUnsupportedBinaryOperation(*self, *other, *op_name);
}

bool Interpreter::doBinaryOperation(BinaryOp op, Context* ctx) {
  Thread* thread = ctx->thread;
  Frame* frame = ctx->frame;
  HandleScope scope(thread);
  Object other(&scope, frame->popValue());
  Object self(&scope, frame->popValue());
  RawObject result = binaryOperation(thread, frame, op, self, other);
  if (result.isError()) return unwind(ctx);
  ctx->frame->pushValue(result);
  return false;
}

RawObject Interpreter::inplaceOperation(Thread* thread, Frame* caller,
                                        BinaryOp op, const Object& self,
                                        const Object& other) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  SymbolId selector = runtime->inplaceOperationSelector(op);
  Object method(&scope, lookupMethod(thread, caller, self, selector));
  if (!method.isError()) {
    RawObject result = callMethod2(thread, caller, method, self, other);
    if (result != runtime->notImplemented()) {
      return result;
    }
  }
  return binaryOperation(thread, caller, op, self, other);
}

bool Interpreter::doInplaceOperation(BinaryOp op, Context* ctx) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Object other(&scope, ctx->frame->popValue());
  Object self(&scope, ctx->frame->popValue());
  RawObject result = inplaceOperation(thread, ctx->frame, op, self, other);
  if (result.isError()) return unwind(ctx);
  ctx->frame->pushValue(result);
  return false;
}

RawObject Interpreter::compareOperation(Thread* thread, Frame* caller,
                                        CompareOp op, const Object& left,
                                        const Object& right) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();

  Type left_type(&scope, runtime->typeOf(*left));
  Type right_type(&scope, runtime->typeOf(*right));

  bool try_swapped = true;
  bool has_different_type = (*left_type != *right_type);
  if (has_different_type && runtime->isSubclass(right_type, left_type)) {
    try_swapped = false;
    SymbolId selector = runtime->swappedComparisonSelector(op);
    Object method(&scope, lookupMethod(thread, caller, right, selector));
    if (!method.isError()) {
      RawObject result = callMethod2(thread, caller, method, right, left);
      if (result != runtime->notImplemented()) {
        return result;
      }
    }
  } else {
    SymbolId selector = runtime->comparisonSelector(op);
    Object method(&scope, lookupMethod(thread, caller, left, selector));
    if (!method.isError()) {
      RawObject result = callMethod2(thread, caller, method, left, right);
      if (result != runtime->notImplemented()) {
        return result;
      }
    }
  }
  if (has_different_type && try_swapped) {
    SymbolId selector = runtime->swappedComparisonSelector(op);
    Object method(&scope, lookupMethod(thread, caller, right, selector));
    if (!method.isError()) {
      RawObject result = callMethod2(thread, caller, method, right, left);
      if (result != runtime->notImplemented()) {
        return result;
      }
    }
  }
  if (op == CompareOp::EQ) {
    return Bool::fromBool(*left == *right);
  }
  if (op == CompareOp::NE) {
    return Bool::fromBool(*left != *right);
  }

  Str op_name(&scope, runtime->symbols()->at(runtime->comparisonSelector(op)));
  return thread->raiseUnsupportedBinaryOperation(*left, *right, *op_name);
}

RawObject Interpreter::sequenceIterSearch(Thread* thread, Frame* caller,
                                          const Object& value,
                                          const Object& container) {
  HandleScope scope(thread);
  Object dunder_iter(
      &scope, lookupMethod(thread, caller, container, SymbolId::kDunderIter));
  if (dunder_iter.isError()) {
    return thread->raiseTypeErrorWithCStr("__iter__ not defined on object");
  }
  Object iter(&scope, callMethod1(thread, caller, dunder_iter, container));
  if (iter.isError()) {
    return *iter;
  }
  Object dunder_next(&scope,
                     lookupMethod(thread, caller, iter, SymbolId::kDunderNext));
  if (dunder_next.isError()) {
    return thread->raiseTypeErrorWithCStr("__next__ not defined on iterator");
  }
  Object current(&scope, NoneType::object());
  Object compare_result(&scope, NoneType::object());
  Object result(&scope, NoneType::object());
  for (;;) {
    current = callMethod1(thread, caller, dunder_next, iter);
    if (current.isError()) {
      if (thread->hasPendingStopIteration()) {
        thread->clearPendingStopIteration();
        break;
      }
      return *current;
    }
    compare_result = compareOperation(thread, caller, EQ, value, current);
    if (compare_result.isError()) {
      return *compare_result;
    }
    result = isTrue(thread, caller, compare_result);
    // isTrue can return Error or Bool, and we would want to return on Error or
    // True.
    if (result != Bool::falseObj()) {
      return *result;
    }
  }
  return Bool::falseObj();
}

RawObject Interpreter::sequenceContains(Thread* thread, Frame* caller,
                                        const Object& value,
                                        const Object& container) {
  HandleScope scope(thread);
  Object method(&scope, lookupMethod(thread, caller, container,
                                     SymbolId::kDunderContains));
  if (!method.isError()) {
    Object result(&scope,
                  callMethod2(thread, caller, method, container, value));
    if (result.isError()) {
      return *result;
    }
    return isTrue(thread, caller, result);
  }
  return sequenceIterSearch(thread, caller, value, container);
}

RawObject Interpreter::isTrue(Thread* thread, Frame* caller,
                              const Object& self) {
  HandleScope scope(thread);
  if (self.isBool()) return *self;
  if (self.isNoneType()) return Bool::falseObj();

  Object method(&scope,
                lookupMethod(thread, caller, self, SymbolId::kDunderBool));
  if (!method.isError()) {
    Object result(&scope, callMethod1(thread, caller, method, self));
    if (result.isError() || result.isBool()) return *result;
    return thread->raiseTypeErrorWithCStr("__bool__ should return bool");
  }

  method = lookupMethod(thread, caller, self, SymbolId::kDunderLen);
  if (!method.isError()) {
    Object result(&scope, callMethod1(thread, caller, method, self));
    if (result.isError()) return *result;
    if (result.isInt()) {
      Int integer(&scope, *result);
      if (integer.isPositive()) return Bool::trueObj();
      if (integer.isZero()) return Bool::falseObj();
      return thread->raiseValueErrorWithCStr("__len__() should return >= 0");
    }
    return thread->raiseTypeErrorWithCStr(
        "object cannot be interpreted as an integer");
  }
  return Bool::trueObj();
}

void Interpreter::raise(Context* ctx, RawObject raw_exc, RawObject raw_cause) {
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  HandleScope scope(ctx->thread);
  Object exc(&scope, raw_exc);
  Object cause(&scope, raw_cause);
  Object type(&scope, NoneType::object());
  Object value(&scope, NoneType::object());

  if (runtime->isInstanceOfType(*exc) &&
      Type(&scope, *exc).isBaseExceptionSubclass()) {
    // raise was given a BaseException subtype. Use it as the type, and call
    // the type object to create the value.
    type = *exc;
    value = Interpreter::callFunction0(thread, ctx->frame, type);
    if (value.isError()) return;
    if (!runtime->isInstanceOfBaseException(*value)) {
      // TODO(bsimmers): Include relevant types here once we have better string
      // formatting.
      thread->raiseTypeErrorWithCStr(
          "calling exception type did not return an instance of BaseException");
      return;
    }
  } else if (runtime->isInstanceOfBaseException(*exc)) {
    // raise was given an instance of a BaseException subtype. Use it as the
    // value and pull out its type.
    value = *exc;
    type = runtime->typeOf(*value);
  } else {
    // raise was given some other, unexpected value.
    ctx->thread->raiseTypeErrorWithCStr(
        "exceptions must derive from BaseException");
    return;
  }

  // Handle the two-arg form of RAISE_VARARGS, corresponding to "raise x from
  // y". If the cause is a type, call it to create an instance. Either way,
  // attach the cause the the primary exception.
  if (!cause.isError()) {  // TODO(T25860930) use Unbound rather than Error.
    if (runtime->isInstanceOfType(*cause) &&
        Type(&scope, *cause).isBaseExceptionSubclass()) {
      cause = Interpreter::callFunction0(thread, ctx->frame, cause);
      if (cause.isError()) return;
    } else if (!runtime->isInstanceOfBaseException(*cause) &&
               !cause.isNoneType()) {
      thread->raiseTypeErrorWithCStr(
          "exception causes must derive from BaseException");
      return;
    }
    BaseException(&scope, *value).setCause(*cause);
  }

  // If we made it here, the process didn't fail with a different exception.
  // Set the pending exception, which is now ready for unwinding. This leaves
  // the VM in a state similar to API functions like PyErr_SetObject(). The
  // main difference is that pendingExceptionValue() will always be an
  // exception instance here, but in the API call case it may be any object
  // (most commonly a str). This discrepancy is cleaned up by
  // normalizeException() in unwind().
  ctx->thread->setPendingExceptionType(*type);
  ctx->thread->setPendingExceptionValue(*value);
}

void Interpreter::unwindExceptHandler(Thread* thread, Frame* frame,
                                      TryBlock block) {
  // Drop all dead values except for the 3 that are popped into the caught
  // exception state.
  DCHECK(block.kind() == TryBlock::kExceptHandler, "Invalid TryBlock Kind");
  frame->dropValues(frame->valueStackSize() - block.level() - 3);
  thread->setCaughtExceptionType(frame->popValue());
  thread->setCaughtExceptionValue(frame->popValue());
  thread->setCaughtExceptionTraceback(frame->popValue());
}

bool Interpreter::popBlock(Context* ctx, TryBlock::Why why,
                           const Object& value) {
  Frame* frame = ctx->frame;
  DCHECK(frame->blockStack()->depth() > 0,
         "Tried to pop from empty blockstack");
  DCHECK(why != TryBlock::Why::kException, "Unsupported Why");

  TryBlock block = frame->blockStack()->peek();
  if (block.kind() == TryBlock::kLoop && why == TryBlock::Why::kContinue) {
    ctx->pc = RawSmallInt::cast(*value).value();
    return true;
  }

  frame->blockStack()->pop();
  if (block.kind() == TryBlock::kExceptHandler) {
    unwindExceptHandler(ctx->thread, frame, block);
    return false;
  }
  frame->dropValues(frame->valueStackSize() - block.level());

  if (block.kind() == TryBlock::kLoop) {
    if (why == TryBlock::Why::kBreak) {
      ctx->pc = block.handler();
      return true;
    }
    return false;
  }

  if (block.kind() == TryBlock::kExcept) {
    // Exception unwinding is handled in Interpreter::unwind() and doesn't come
    // through here. Ignore the Except block.
    return false;
  }

  DCHECK(block.kind() == TryBlock::kFinally, "Unexpected TryBlock kind");
  if (why == TryBlock::Why::kReturn || why == TryBlock::Why::kContinue) {
    frame->pushValue(*value);
  }
  frame->pushValue(RawSmallInt::fromWord(static_cast<int>(why)));
  ctx->pc = block.handler();
  return true;
}

// If the current frame is executing a Generator, mark it as finished.
static void finishCurrentGenerator(Interpreter::Context* ctx) {
  if (!RawCode::cast(ctx->frame->code()).hasGenerator()) return;

  // Write to the Generator's HeapFrame directly so we don't have to save the
  // live frame to it one last time.
  HandleScope scope(ctx->thread);
  GeneratorBase gen(&scope,
                    ctx->thread->runtime()->genFromStackFrame(ctx->frame));
  HeapFrame heap_frame(&scope, gen.heapFrame());
  heap_frame.frame()->setVirtualPC(Frame::kFinishedGeneratorPC);
}

bool Interpreter::handleReturn(Context* ctx, const Object& retval) {
  Frame* frame = ctx->frame;
  for (;;) {
    if (frame->blockStack()->depth() == 0) {
      finishCurrentGenerator(ctx);
      frame->pushValue(*retval);
      return true;
    }
    if (popBlock(ctx, TryBlock::Why::kReturn, retval)) return false;
  }
}

void Interpreter::handleLoopExit(Context* ctx, TryBlock::Why why,
                                 const Object& retval) {
  for (;;) {
    if (popBlock(ctx, why, retval)) return;
  }
}

bool Interpreter::unwind(Context* ctx) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Frame* frame = ctx->frame;
  BlockStack* stack = frame->blockStack();

  // TODO(bsimmers): Record traceback for newly-raised exceptions, like what
  // CPython does with PyTraceBack_Here().

  // TODO(T31788973): Extend this to unwind more than one Frame before giving
  // up.
  while (stack->depth() > 0) {
    TryBlock block = stack->pop();
    if (block.kind() == TryBlock::kExceptHandler) {
      unwindExceptHandler(thread, frame, block);
      continue;
    }
    frame->dropValues(frame->valueStackSize() - block.level());

    if (block.kind() == TryBlock::kLoop) continue;
    DCHECK(
        block.kind() == TryBlock::kExcept || block.kind() == TryBlock::kFinally,
        "Unexpected TryBlock::Kind");

    // Push a handler block and save the current caught exception, if any.
    stack->push(TryBlock{TryBlock::kExceptHandler, 0, frame->valueStackSize()});
    frame->pushValue(thread->caughtExceptionTraceback());
    frame->pushValue(thread->caughtExceptionValue());
    frame->pushValue(thread->caughtExceptionType());

    // Load and normalize the pending exception.
    Object type(&scope, thread->pendingExceptionType());
    Object value(&scope, thread->pendingExceptionValue());
    Object traceback(&scope, thread->pendingExceptionTraceback());
    thread->clearPendingException();
    normalizeException(thread, &type, &value, &traceback);
    BaseException(&scope, *value).setTraceback(*traceback);

    // Promote the normalized exception to caught, push it for the bytecode
    // handler, and jump to the handler.
    thread->setCaughtExceptionType(*type);
    thread->setCaughtExceptionValue(*value);
    thread->setCaughtExceptionTraceback(*traceback);
    frame->pushValue(*traceback);
    frame->pushValue(*value);
    frame->pushValue(*type);
    ctx->pc = block.handler();
    return false;
  }

  finishCurrentGenerator(ctx);
  frame->pushValue(Error::object());
  return true;
}

static Bytecode currentBytecode(const Interpreter::Context* ctx) {
  RawBytes code = RawBytes::cast(RawCode::cast(ctx->frame->code())->code());
  word pc = ctx->pc;
  word result;
  do {
    pc -= 2;
    result = code->byteAt(pc);
  } while (result == Bytecode::EXTENDED_ARG);
  return static_cast<Bytecode>(result);
}

void Interpreter::doInvalidBytecode(Context* ctx, word) {
  Bytecode bc = currentBytecode(ctx);
  UNREACHABLE("bytecode '%s'", kBytecodeNames[bc]);
}

void Interpreter::doNotImplemented(Context* ctx, word) {
  Bytecode bc = currentBytecode(ctx);
  UNIMPLEMENTED("bytecode '%s'", kBytecodeNames[bc]);
}

// opcode 1
void Interpreter::doPopTop(Context* ctx, word) { ctx->frame->popValue(); }

// opcode 2
void Interpreter::doRotTwo(Context* ctx, word) {
  RawObject* sp = ctx->frame->valueStackTop();
  RawObject top = *sp;
  *sp = *(sp + 1);
  *(sp + 1) = top;
}

// opcode 3
void Interpreter::doRotThree(Context* ctx, word) {
  RawObject* sp = ctx->frame->valueStackTop();
  RawObject top = *sp;
  *sp = *(sp + 1);
  *(sp + 1) = *(sp + 2);
  *(sp + 2) = top;
}

// opcode 4
void Interpreter::doDupTop(Context* ctx, word) {
  ctx->frame->pushValue(ctx->frame->topValue());
}

// opcode 5
void Interpreter::doDupTopTwo(Context* ctx, word) {
  RawObject first = ctx->frame->topValue();
  RawObject second = ctx->frame->peek(1);
  ctx->frame->pushValue(second);
  ctx->frame->pushValue(first);
}

// opcode 9
void Interpreter::doNop(Context*, word) {}

// opcode 10
bool Interpreter::doUnaryPositive(Context* ctx, word) {
  return doUnaryOperation(SymbolId::kDunderPos, ctx);
}

// opcode 11
bool Interpreter::doUnaryNegative(Context* ctx, word) {
  return doUnaryOperation(SymbolId::kDunderNeg, ctx);
}

// opcode 12
bool Interpreter::doUnaryNot(Context* ctx, word) {
  HandleScope scope(ctx->thread);
  Object self(&scope, ctx->frame->topValue());
  RawObject result = isTrue(ctx->thread, ctx->frame, self);
  if (result.isError()) return unwind(ctx);
  ctx->frame->setTopValue(Bool::fromBool(result != Bool::trueObj()));
  return false;
}

// opcode 15
bool Interpreter::doUnaryInvert(Context* ctx, word) {
  return doUnaryOperation(SymbolId::kDunderInvert, ctx);
}

// opcode 16
bool Interpreter::doBinaryMatrixMultiply(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::MATMUL, ctx);
}

// opcode 17
bool Interpreter::doInplaceMatrixMultiply(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::MATMUL, ctx);
}

// opcode 19
bool Interpreter::doBinaryPower(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::POW, ctx);
}

// opcode 20
bool Interpreter::doBinaryMultiply(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::MUL, ctx);
}

// opcode 22
bool Interpreter::doBinaryModulo(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::MOD, ctx);
}

// opcode 23
bool Interpreter::doBinaryAdd(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::ADD, ctx);
}

// opcode 24
bool Interpreter::doBinarySubtract(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::SUB, ctx);
}

// opcode 25
bool Interpreter::doBinarySubscr(Context* ctx, word) {
  HandleScope scope(ctx->thread);
  Runtime* runtime = ctx->thread->runtime();
  Object key(&scope, ctx->frame->popValue());
  Object container(&scope, ctx->frame->popValue());
  Type type(&scope, runtime->typeOf(*container));
  Object getitem(&scope, runtime->lookupSymbolInMro(ctx->thread, type,
                                                    SymbolId::kDunderGetItem));
  if (getitem.isError()) {
    ctx->thread->raiseTypeErrorWithCStr("object does not support indexing");
    return unwind(ctx);
  }

  Object result(&scope,
                callMethod2(ctx->thread, ctx->frame, getitem, container, key));
  if (result.isError()) return unwind(ctx);
  ctx->frame->pushValue(*result);
  return false;
}

// opcode 26
bool Interpreter::doBinaryFloorDivide(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::FLOORDIV, ctx);
}

// opcode 27
bool Interpreter::doBinaryTrueDivide(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::TRUEDIV, ctx);
}

// opcode 28
bool Interpreter::doInplaceFloorDivide(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::FLOORDIV, ctx);
}

// opcode 29
bool Interpreter::doInplaceTrueDivide(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::TRUEDIV, ctx);
}

// opcode 50
bool Interpreter::doGetAiter(Context* ctx, word) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Object obj(&scope, ctx->frame->topValue());
  Object method(&scope,
                lookupMethod(thread, ctx->frame, obj, SymbolId::kDunderAiter));
  if (method.isError()) {
    thread->raiseTypeErrorWithCStr(
        "'async for' requires an object with __aiter__ method");
    return unwind(ctx);
  }
  Object aiter(&scope, callMethod1(thread, ctx->frame, method, obj));
  if (aiter.isError()) return unwind(ctx);
  ctx->frame->setTopValue(*aiter);
  return false;
}

// opcode 51
bool Interpreter::doGetAnext(Context* ctx, word) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Object obj(&scope, ctx->frame->topValue());
  Object anext(&scope,
               lookupMethod(thread, ctx->frame, obj, SymbolId::kDunderAnext));
  if (anext.isError()) {
    thread->raiseTypeErrorWithCStr(
        "'async for' requires an iterator with __anext__ method");
    return unwind(ctx);
  }
  Object awaitable(&scope, callMethod1(thread, ctx->frame, anext, obj));
  if (awaitable.isError()) return unwind(ctx);

  // TODO(T33628943): Check if `awaitable` is a native or generator-based
  // coroutine and if it is, no need to call __await__
  Object await(&scope,
               lookupMethod(thread, ctx->frame, obj, SymbolId::kDunderAwait));
  if (await.isError()) {
    thread->raiseTypeErrorWithCStr(
        "'async for' received an invalid object from __anext__");
    return unwind(ctx);
  }
  Object aiter(&scope, callMethod1(thread, ctx->frame, await, awaitable));
  if (aiter.isError()) return unwind(ctx);

  ctx->frame->setTopValue(*aiter);
  return false;
}

// opcode 52
bool Interpreter::doBeforeAsyncWith(Context* ctx, word) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Frame* frame = ctx->frame;
  Object manager(&scope, frame->popValue());

  // resolve __aexit__ an push it
  Runtime* runtime = thread->runtime();
  Object exit_selector(&scope, runtime->symbols()->DunderAexit());
  Object exit(&scope, runtime->attributeAt(thread, manager, exit_selector));
  if (exit.isError()) {
    UNIMPLEMENTED("throw TypeError");
  }
  frame->pushValue(*exit);

  // resolve __aenter__ call it and push the return value
  Object enter(&scope,
               lookupMethod(thread, frame, manager, SymbolId::kDunderAenter));
  if (enter.isError()) {
    UNIMPLEMENTED("throw TypeError");
  }
  Object result(&scope, callMethod1(thread, frame, enter, manager));
  if (result.isError()) return unwind(ctx);
  frame->pushValue(*result);
  return false;
}

// opcode 55
bool Interpreter::doInplaceAdd(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::ADD, ctx);
}

// opcode 56
bool Interpreter::doInplaceSubtract(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::SUB, ctx);
}

// opcode 57
bool Interpreter::doInplaceMultiply(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::MUL, ctx);
}

// opcode 59
bool Interpreter::doInplaceModulo(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::MOD, ctx);
}

// opcode 60
bool Interpreter::doStoreSubscr(Context* ctx, word) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Object key(&scope, ctx->frame->popValue());
  Object container(&scope, ctx->frame->popValue());
  Object setitem(&scope, lookupMethod(thread, ctx->frame, container,
                                      SymbolId::kDunderSetItem));
  if (setitem.isError()) {
    UNIMPLEMENTED("throw TypeError");
  }
  Object value(&scope, ctx->frame->popValue());
  if (callMethod3(thread, ctx->frame, setitem, container, key, value)
          .isError()) {
    return unwind(ctx);
  }
  return false;
}

// opcode 61
bool Interpreter::doDeleteSubscr(Context* ctx, word) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Object key(&scope, ctx->frame->popValue());
  Object container(&scope, ctx->frame->popValue());
  Object delitem(&scope, lookupMethod(thread, ctx->frame, container,
                                      SymbolId::kDunderDelItem));
  if (delitem.isError()) {
    UNIMPLEMENTED("throw TypeError");
  }
  Object result(&scope,
                callMethod2(thread, ctx->frame, delitem, container, key));
  if (result.isError()) return unwind(ctx);
  ctx->frame->pushValue(*result);
  return false;
}

// opcode 62
bool Interpreter::doBinaryLshift(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::LSHIFT, ctx);
}

// opcode 63
bool Interpreter::doBinaryRshift(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::RSHIFT, ctx);
}

// opcode 64
bool Interpreter::doBinaryAnd(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::AND, ctx);
}

// opcode 65
bool Interpreter::doBinaryXor(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::XOR, ctx);
}

// opcode 66
bool Interpreter::doBinaryOr(Context* ctx, word) {
  return doBinaryOperation(BinaryOp::OR, ctx);
}

// opcode 67
bool Interpreter::doInplacePower(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::POW, ctx);
}

// opcode 68
bool Interpreter::doGetIter(Context* ctx, word) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Object iterable(&scope, ctx->frame->topValue());
  Object method(&scope, lookupMethod(thread, ctx->frame, iterable,
                                     SymbolId::kDunderIter));
  if (method.isError()) {
    thread->raiseTypeErrorWithCStr("object is not iterable");
    return unwind(ctx);
  }
  Object iterator(&scope, callMethod1(thread, ctx->frame, method, iterable));
  if (iterator.isError()) return unwind(ctx);
  ctx->frame->setTopValue(*iterator);
  return false;
}

// opcode 69
bool Interpreter::doGetYieldFromIter(Context* ctx, word) {
  Thread* thread = ctx->thread;
  Frame* frame = ctx->frame;
  HandleScope scope(thread);
  Object iterable(&scope, frame->topValue());

  if (iterable.isGenerator()) return false;

  if (iterable.isCoroutine()) {
    Code code(&scope, frame->code());
    if (code.hasCoroutine() || code.hasIterableCoroutine()) {
      thread->raiseTypeErrorWithCStr(
          "cannot 'yield from' a coroutine object in a non-coroutine "
          "generator");
      return unwind(ctx);
    }
    return false;
  }

  Object method(&scope,
                lookupMethod(thread, frame, iterable, SymbolId::kDunderIter));
  if (method.isError()) {
    thread->raiseTypeErrorWithCStr("object is not iterable");
    return unwind(ctx);
  }
  Object iterator(&scope, callMethod1(thread, frame, method, iterable));
  if (iterator.isError()) return unwind(ctx);
  frame->setTopValue(*iterator);
  return false;
}

// opcode 70
void Interpreter::doPrintExpr(Context* ctx, word) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Frame* frame = ctx->frame;
  Object value(&scope, frame->popValue());
  ValueCell value_cell(&scope, thread->runtime()->displayHook());
  if (value_cell.isUnbound()) {
    UNIMPLEMENTED("RuntimeError: lost sys.displayhook");
  }
  frame->pushValue(value_cell.value());
  frame->pushValue(*value);
  call(thread, frame, 1);
}

// opcode 71
void Interpreter::doLoadBuildClass(Context* ctx, word) {
  Thread* thread = ctx->thread;
  RawValueCell value_cell = RawValueCell::cast(thread->runtime()->buildClass());
  ctx->frame->pushValue(value_cell->value());
}

// opcode 72
bool Interpreter::doYieldFrom(Context* ctx, word) {
  Thread* thread = ctx->thread;
  Frame* frame = ctx->frame;
  HandleScope scope(thread);

  Object value(&scope, frame->popValue());
  Object iterator(&scope, frame->topValue());
  Object result(&scope, NoneType::object());
  if (value.isNoneType()) {
    Object next_method(
        &scope, lookupMethod(thread, frame, iterator, SymbolId::kDunderNext));
    if (next_method.isError()) {
      thread->raiseTypeErrorWithCStr("iter() returned non-iterator");
      return unwind(ctx);
    }
    result = callMethod1(thread, frame, next_method, iterator);
  } else {
    Object send_method(&scope,
                       lookupMethod(thread, frame, iterator, SymbolId::kSend));
    if (send_method.isError()) {
      thread->raiseTypeErrorWithCStr("iter() returned non-iterator");
      return unwind(ctx);
    }
    result = callMethod2(thread, frame, send_method, iterator, value);
  }
  if (result.isError()) {
    if (!thread->hasPendingStopIteration()) return unwind(ctx);

    frame->setTopValue(thread->pendingExceptionValue());
    thread->clearPendingException();
    return false;
  }

  // Unlike YIELD_VALUE, don't update PC in the frame: we want this
  // instruction to re-execute until the subiterator is exhausted.
  GeneratorBase gen(&scope, thread->runtime()->genFromStackFrame(frame));
  thread->runtime()->genSave(thread, gen);
  frame->pushValue(*result);
  return true;
}

// opcode 73
bool Interpreter::doGetAwaitable(Context* ctx, word) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Object obj(&scope, ctx->frame->topValue());

  // TODO(T33628943): Check if `obj` is a native or generator-based
  // coroutine and if it is, no need to call __await__
  Object await(&scope,
               lookupMethod(thread, ctx->frame, obj, SymbolId::kDunderAwait));
  if (await.isError()) {
    thread->raiseTypeErrorWithCStr(
        "object can't be used in 'await' expression");
    return unwind(ctx);
  }
  Object iter(&scope, callMethod1(thread, ctx->frame, await, obj));
  if (iter.isError()) return unwind(ctx);

  ctx->frame->setTopValue(*iter);
  return false;
}

// opcode 75
bool Interpreter::doInplaceLshift(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::LSHIFT, ctx);
}

// opcode 76
bool Interpreter::doInplaceRshift(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::RSHIFT, ctx);
}

// opcode 77
bool Interpreter::doInplaceAnd(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::AND, ctx);
}

// opcode 78
bool Interpreter::doInplaceXor(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::XOR, ctx);
}

// opcode 79
bool Interpreter::doInplaceOr(Context* ctx, word) {
  return doInplaceOperation(BinaryOp::OR, ctx);
}

// opcode 80
void Interpreter::doBreakLoop(Context* ctx, word) {
  HandleScope scope(ctx->thread);
  Object none(&scope, NoneType::object());
  handleLoopExit(ctx, TryBlock::Why::kBreak, none);
}

// opcode 81
bool Interpreter::doWithCleanupStart(Context* ctx, word) {
  HandleScope scope(ctx->thread);
  Frame* frame = ctx->frame;
  Object exc(&scope, frame->popValue());
  Object value(&scope, NoneType::object());
  Object traceback(&scope, NoneType::object());
  Object exit(&scope, NoneType::object());

  // The stack currently contains a sequence of values understood by
  // END_FINALLY, followed by __exit__ from the context manager. We need to
  // determine the location of __exit__ and remove it from the stack, shifting
  // everything above it down to compensate.
  if (exc.isNoneType()) {
    // The with block exited normally. __exit__ is just below the None.
    exit = frame->topValue();
  } else if (exc.isSmallInt()) {
    // The with block exited for a return, continue, or break. __exit__ will be
    // below 'why' and an optional return value (depending on 'why').
    auto why = static_cast<TryBlock::Why>(RawSmallInt::cast(*exc).value());
    if (why == TryBlock::Why::kReturn || why == TryBlock::Why::kContinue) {
      exit = frame->peek(1);
      frame->setValueAt(frame->peek(0), 1);
    } else {
      exit = frame->topValue();
    }
  } else {
    // The stack contains the caught exception, the previous exception state,
    // then __exit__. Grab __exit__ then shift everything else down.
    exit = frame->peek(5);
    for (word i = 5; i > 0; i--) {
      frame->setValueAt(frame->peek(i - 1), i);
    }
    value = frame->peek(1);
    traceback = frame->peek(2);

    // We popped __exit__ out from under the depth recorded by the top
    // ExceptHandler block, so adjust it.
    TryBlock block = frame->blockStack()->pop();
    DCHECK(block.kind() == TryBlock::kExceptHandler,
           "Unexpected TryBlock Kind");
    block.setLevel(block.level() - 1);
    frame->blockStack()->push(block);
  }

  // Regardless of what happened above, exc should be put back at the new top of
  // the stack.
  frame->setTopValue(*exc);

  Object result(&scope,
                callFunction3(ctx->thread, frame, exit, exc, value, traceback));
  if (result.isError()) return unwind(ctx);

  // Push exc and result to be consumed by WITH_CLEANUP_FINISH.
  ctx->frame->pushValue(*exc);
  ctx->frame->pushValue(*result);

  return false;
}

// opcode 82
bool Interpreter::doWithCleanupFinish(Context* ctx, word) {
  Frame* frame = ctx->frame;
  HandleScope scope(ctx->thread);
  Object result(&scope, frame->popValue());
  Object exc(&scope, frame->popValue());
  if (!exc.isNoneType()) {
    Object is_true(&scope, isTrue(ctx->thread, frame, result));
    if (is_true.isError()) return unwind(ctx);
    if (*is_true == Bool::trueObj()) {
      frame->pushValue(
          SmallInt::fromWord(static_cast<int>(TryBlock::Why::kSilenced)));
    }
  }

  return false;
}

// opcode 83
bool Interpreter::doReturnValue(Context* ctx, word) {
  HandleScope scope(ctx->thread);
  Object result(&scope, ctx->frame->popValue());
  return handleReturn(ctx, result);
}

// opcode 85
void Interpreter::doSetupAnnotations(Context* ctx, word) {
  HandleScope scope(ctx->thread);
  Runtime* runtime = ctx->thread->runtime();
  Dict implicit_globals(&scope, ctx->frame->implicitGlobals());
  Object annotations(&scope,
                     runtime->symbols()->at(SymbolId::kDunderAnnotations));
  Object anno_dict(&scope, runtime->dictAt(implicit_globals, annotations));
  if (anno_dict.isError()) {
    Object new_dict(&scope, runtime->newDict());
    runtime->dictAtPutInValueCell(implicit_globals, annotations, new_dict);
  }
}

// opcode 86
bool Interpreter::doYieldValue(Context* ctx, word) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  HandleScope scope(thread);

  Object result(&scope, frame->popValue());
  frame->setVirtualPC(ctx->pc);
  GeneratorBase gen(&scope, thread->runtime()->genFromStackFrame(frame));
  thread->runtime()->genSave(thread, gen);
  frame->pushValue(*result);
  return true;
}

// opcode 84
void Interpreter::doImportStar(Context* ctx, word) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);

  // Pre-python3 this used to merge the locals with the locals dict. However,
  // that's not necessary anymore. You can't import * inside a function
  // body anymore.

  Module module(&scope, ctx->frame->popValue());
  CHECK(module.isModule(), "Unexpected type to import from");

  Frame* frame = ctx->frame;
  Dict implicit_globals(&scope, frame->implicitGlobals());
  thread->runtime()->moduleImportAllFrom(implicit_globals, module);
}

// opcode 87
void Interpreter::doPopBlock(Context* ctx, word) {
  Frame* frame = ctx->frame;
  TryBlock block = frame->blockStack()->pop();
  frame->setValueStackTop(frame->valueStackBase() - block.level());
}

// opcode 88
bool Interpreter::doEndFinally(Context* ctx, word) {
  Thread* thread = ctx->thread;
  Frame* frame = ctx->frame;
  HandleScope scope(thread);

  Object status(&scope, frame->popValue());
  if (status.isSmallInt()) {
    auto why = static_cast<TryBlock::Why>(SmallInt::cast(*status).value());
    if (why == TryBlock::Why::kSilenced) {
      unwindExceptHandler(thread, frame, frame->blockStack()->pop());
      return false;
    }

    Object retval(&scope, NoneType::object());
    if (why == TryBlock::Why::kReturn || why == TryBlock::Why::kContinue) {
      retval = frame->popValue();
    }

    if (why == TryBlock::Why::kReturn) return handleReturn(ctx, retval);
    handleLoopExit(ctx, why, retval);
    return false;
  }
  if (thread->runtime()->isInstanceOfType(*status) &&
      Type(&scope, *status).isBaseExceptionSubclass()) {
    thread->setPendingExceptionType(*status);
    thread->setPendingExceptionValue(frame->popValue());
    thread->setPendingExceptionTraceback(frame->popValue());
    return unwind(ctx);
  }
  if (!status.isNoneType()) {
    thread->raiseSystemErrorWithCStr("Bad exception given to 'finally'");
    return unwind(ctx);
  }

  return false;
}

// opcode 89
bool Interpreter::doPopExcept(Context* ctx, word) {
  Thread* thread = ctx->thread;
  Frame* frame = ctx->frame;

  TryBlock block = ctx->frame->blockStack()->pop();
  if (block.kind() != TryBlock::kExceptHandler) {
    thread->raiseSystemErrorWithCStr("popped block is not an except handler");
    return unwind(ctx);
  }

  unwindExceptHandler(thread, frame, block);
  return false;
}

// opcode 90
void Interpreter::doStoreName(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  DCHECK(frame->implicitGlobals()->isDict(), "expected dict");
  HandleScope scope;
  Dict implicit_globals(&scope, frame->implicitGlobals());
  RawObject names = RawCode::cast(frame->code())->names();
  Object key(&scope, RawTuple::cast(names)->at(arg));
  Object value(&scope, frame->popValue());
  thread->runtime()->dictAtPutInValueCell(implicit_globals, key, value);
}

// opcode 91
void Interpreter::doDeleteName(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  DCHECK(frame->implicitGlobals()->isDict(), "expected dict");
  HandleScope scope;
  Dict implicit_globals(&scope, frame->implicitGlobals());
  RawObject names = RawCode::cast(frame->code())->names();
  Object key(&scope, RawTuple::cast(names)->at(arg));
  if (thread->runtime()->dictRemove(implicit_globals, key)->isError()) {
    UNIMPLEMENTED("item not found in delete name");
  }
}

// opcode 92
bool Interpreter::doUnpackSequence(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  Frame* frame = ctx->frame;
  HandleScope scope(thread);
  Object iterable(&scope, frame->popValue());
  Object iter_method(
      &scope, lookupMethod(thread, frame, iterable, SymbolId::kDunderIter));
  if (iter_method.isError()) {
    thread->raiseTypeErrorWithCStr("object is not iterable");
    return unwind(ctx);
  }
  Object iterator(&scope, callMethod1(thread, frame, iter_method, iterable));
  if (iterator.isError()) return unwind(ctx);

  Object next_method(
      &scope, lookupMethod(thread, frame, iterator, SymbolId::kDunderNext));
  if (next_method.isError()) {
    thread->raiseTypeErrorWithCStr("iter() returned non-iterator");
    return unwind(ctx);
  }
  word num_pushed = 0;
  Object value(&scope, RawNoneType::object());
  for (;;) {
    value = callMethod1(thread, frame, next_method, iterator);
    if (value.isError()) {
      if (thread->clearPendingStopIteration()) {
        if (num_pushed == arg) break;
        thread->raiseValueErrorWithCStr("not enough values to unpack");
      }
      return unwind(ctx);
    }
    if (num_pushed == arg) {
      thread->raiseValueErrorWithCStr("too many values to unpack");
      return unwind(ctx);
    }
    frame->pushValue(*value);
    ++num_pushed;
  }

  // swap values on the stack
  Object tmp(&scope, NoneType::object());
  for (word i = 0, j = num_pushed - 1, half = num_pushed / 2; i < half;
       ++i, --j) {
    tmp = frame->peek(i);
    frame->setValueAt(frame->peek(j), i);
    frame->setValueAt(*tmp, j);
  }
  return false;
}

// opcode 93
bool Interpreter::doForIter(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Object iterator(&scope, ctx->frame->topValue());
  Object next_method(&scope, lookupMethod(thread, ctx->frame, iterator,
                                          SymbolId::kDunderNext));
  if (next_method.isError()) {
    thread->raiseTypeErrorWithCStr("iter() returned non-iterator");
    return unwind(ctx);
  }
  Object value(&scope, callMethod1(thread, ctx->frame, next_method, iterator));
  if (value.isError()) {
    if (thread->clearPendingStopIteration()) {
      ctx->frame->popValue();
      ctx->pc += arg;
      return false;
    }
    return unwind(ctx);
  }
  ctx->frame->pushValue(*value);
  return false;
}

// opcode 94
bool Interpreter::doUnpackEx(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  Frame* frame = ctx->frame;
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  Object iterable(&scope, frame->popValue());
  Object iter_method(
      &scope, lookupMethod(thread, frame, iterable, SymbolId::kDunderIter));
  if (iter_method.isError()) {
    thread->raiseTypeErrorWithCStr("object is not iterable");
    return unwind(ctx);
  }
  Object iterator(&scope, callMethod1(thread, frame, iter_method, iterable));
  if (iterator.isError()) return unwind(ctx);

  Object next_method(
      &scope, lookupMethod(thread, frame, iterator, SymbolId::kDunderNext));
  if (next_method.isError()) {
    thread->raiseTypeErrorWithCStr("iter() returned non-iterator");
    return unwind(ctx);
  }

  word before = arg & kMaxByte;
  word after = (arg >> kBitsPerByte) & kMaxByte;
  word num_pushed = 0;
  Object value(&scope, RawNoneType::object());
  for (; num_pushed < before; ++num_pushed) {
    value = callMethod1(thread, frame, next_method, iterator);
    if (value.isError()) {
      if (thread->clearPendingStopIteration()) break;
      return unwind(ctx);
    }
    frame->pushValue(*value);
  }

  if (num_pushed < before) {
    thread->raiseValueErrorWithCStr("not enough values to unpack");
    return unwind(ctx);
  }

  List list(&scope, runtime->newList());
  for (;;) {
    value = callMethod1(thread, frame, next_method, iterator);
    if (value.isError()) {
      if (thread->clearPendingStopIteration()) break;
      return unwind(ctx);
    }
    runtime->listAdd(list, value);
  }

  frame->pushValue(*list);
  num_pushed++;

  if (list.numItems() < after) {
    thread->raiseValueErrorWithCStr("not enough values to unpack");
    return unwind(ctx);
  }

  if (after > 0) {
    // Pop elements off the list and set them on the stack
    for (word i = list.numItems() - after, j = list.numItems(); i < j;
         ++i, ++num_pushed) {
      frame->pushValue(list.at(i));
      list.atPut(i, NoneType::object());
    }
    list.setNumItems(list.numItems() - after);
  }

  // swap values on the stack
  Object tmp(&scope, NoneType::object());
  for (word i = 0, j = num_pushed - 1, half = num_pushed / 2; i < half;
       ++i, --j) {
    tmp = frame->peek(i);
    frame->setValueAt(frame->peek(j), i);
    frame->setValueAt(*tmp, j);
  }
  return false;
}

// opcode 95
bool Interpreter::doStoreAttr(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope;
  Object receiver(&scope, ctx->frame->popValue());
  auto names = RawCode::cast(ctx->frame->code())->names();
  Object name(&scope, RawTuple::cast(names)->at(arg));
  Object value(&scope, ctx->frame->popValue());
  if (thread->runtime()
          ->attributeAtPut(thread, receiver, name, value)
          .isError()) {
    return unwind(ctx);
  }
  return false;
}

// opcode 96
bool Interpreter::doDeleteAttr(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope;
  Object receiver(&scope, ctx->frame->popValue());
  auto names = RawCode::cast(ctx->frame->code())->names();
  Object name(&scope, RawTuple::cast(names)->at(arg));
  if (thread->runtime()->attributeDel(ctx->thread, receiver, name).isError()) {
    return unwind(ctx);
  }
  return false;
}

// opcode 97
void Interpreter::doStoreGlobal(Context* ctx, word arg) {
  RawValueCell::cast(RawTuple::cast(ctx->frame->fastGlobals())->at(arg))
      ->setValue(ctx->frame->popValue());
}

// opcode 98
void Interpreter::doDeleteGlobal(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  HandleScope scope;
  ValueCell value_cell(
      &scope,
      RawValueCell::cast(RawTuple::cast(frame->fastGlobals())->at(arg)));
  CHECK(!value_cell.value()->isValueCell(), "Unbound Globals");
  Object key(&scope,
             RawTuple::cast(RawCode::cast(frame->code())->names())->at(arg));
  Dict builtins(&scope, frame->builtins());
  Runtime* runtime = thread->runtime();
  Object value_in_builtin(&scope, runtime->dictAt(builtins, key));
  if (value_in_builtin.isError()) {
    value_in_builtin =
        runtime->dictAtPutInValueCell(builtins, key, value_in_builtin);
    RawValueCell::cast(*value_in_builtin)->makeUnbound();
  }
  value_cell.setValue(*value_in_builtin);
}

// opcode 100
void Interpreter::doLoadConst(Context* ctx, word arg) {
  RawObject consts = RawCode::cast(ctx->frame->code())->consts();
  ctx->frame->pushValue(RawTuple::cast(consts)->at(arg));
}

static RawObject raiseUndefinedName(Thread* thread, const Str& name) {
  return thread->raise(LayoutId::kNameError,
                       thread->runtime()->newStrFromFormat(
                           "name '%s' is not defined",
                           unique_c_ptr<char[]>(name.toCStr()).get()));
}

// opcode 101
bool Interpreter::doLoadName(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Runtime* runtime = ctx->thread->runtime();
  HandleScope scope(ctx->thread);

  Object names(&scope, RawCode::cast(frame->code())->names());
  Str key(&scope, RawTuple::cast(*names)->at(arg));

  // 1. implicitGlobals
  Dict implicit_globals(&scope, frame->implicitGlobals());
  RawObject value = runtime->dictAt(implicit_globals, key);
  if (value->isValueCell()) {
    // 3a. found in [implicit]/globals but with up to 2-layers of indirection
    DCHECK(!RawValueCell::cast(value)->isUnbound(), "unbound globals");
    value = RawValueCell::cast(value)->value();
    if (value->isValueCell()) {
      DCHECK(!RawValueCell::cast(value)->isUnbound(), "unbound builtins");
      value = RawValueCell::cast(value)->value();
    }
    frame->pushValue(value);
    return false;
  }

  // In the module body, globals == implicit_globals, so no need to check
  // twice. However in class body, it is a different dict.
  if (frame->implicitGlobals() != frame->globals()) {
    // 2. globals
    Dict globals(&scope, frame->globals());
    value = runtime->dictAt(globals, key);
  }
  if (value->isValueCell()) {
    // 3a. found in [implicit]/globals but with up to 2-layers of indirection
    DCHECK(!RawValueCell::cast(value)->isUnbound(), "unbound globals");
    value = RawValueCell::cast(value)->value();
    if (value->isValueCell()) {
      DCHECK(!RawValueCell::cast(value)->isUnbound(), "unbound builtins");
      value = RawValueCell::cast(value)->value();
    }
    frame->pushValue(value);
    return false;
  }

  // 3b. not found; check builtins -- one layer of indirection
  Dict builtins(&scope, frame->builtins());
  value = runtime->dictAt(builtins, key);
  if (value->isValueCell()) {
    DCHECK(!RawValueCell::cast(value)->isUnbound(), "unbound builtins");
    value = RawValueCell::cast(value)->value();
  }

  if (value->isError()) {
    raiseUndefinedName(ctx->thread, key);
    return unwind(ctx);
  }
  frame->pushValue(value);
  return false;
}

// opcode 102
void Interpreter::doBuildTuple(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope;
  Tuple tuple(&scope, thread->runtime()->newTuple(arg));
  for (word i = arg - 1; i >= 0; i--) {
    tuple.atPut(i, ctx->frame->popValue());
  }
  ctx->frame->pushValue(*tuple);
}

// opcode 103
void Interpreter::doBuildList(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope;
  Tuple array(&scope, thread->runtime()->newTuple(arg));
  for (word i = arg - 1; i >= 0; i--) {
    array.atPut(i, ctx->frame->popValue());
  }
  RawList list = RawList::cast(thread->runtime()->newList());
  list->setItems(*array);
  list->setNumItems(array.length());
  ctx->frame->pushValue(list);
}

// opcode 104
void Interpreter::doBuildSet(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope;
  Runtime* runtime = thread->runtime();
  Set set(&scope, RawSet::cast(runtime->newSet()));
  for (word i = arg - 1; i >= 0; i--) {
    runtime->setAdd(set, Object(&scope, ctx->frame->popValue()));
  }
  ctx->frame->pushValue(*set);
}

// opcode 105
void Interpreter::doBuildMap(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  HandleScope scope;
  Dict dict(&scope, runtime->newDictWithSize(arg));
  for (word i = 0; i < arg; i++) {
    Object value(&scope, ctx->frame->popValue());
    Object key(&scope, ctx->frame->popValue());
    runtime->dictAtPut(dict, key, value);
  }
  ctx->frame->pushValue(*dict);
}

// opcode 106
bool Interpreter::doLoadAttr(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope;
  Object receiver(&scope, ctx->frame->topValue());
  auto names = RawCode::cast(ctx->frame->code())->names();
  Object name(&scope, RawTuple::cast(names)->at(arg));
  RawObject result = thread->runtime()->attributeAt(thread, receiver, name);
  if (result.isError()) return unwind(ctx);
  ctx->frame->setTopValue(result);
  return false;
}

static RawObject excMatch(Interpreter::Context* ctx, const Object& left,
                          const Object& right) {
  Runtime* runtime = ctx->thread->runtime();
  HandleScope scope(ctx->thread);

  static const char* cannot_catch_msg =
      "catching classes that do not inherit from BaseException is not allowed";
  if (runtime->isInstanceOfTuple(*right)) {
    // TODO(bsimmers): handle tuple subclasses
    Tuple tuple(&scope, *right);
    for (word i = 0, length = tuple.length(); i < length; i++) {
      Object obj(&scope, tuple.at(i));
      if (!(runtime->isInstanceOfType(*obj) &&
            Type(&scope, *obj).isBaseExceptionSubclass())) {
        return ctx->thread->raiseTypeErrorWithCStr(cannot_catch_msg);
      }
    }
  } else if (!(runtime->isInstanceOfType(*right) &&
               Type(&scope, *right).isBaseExceptionSubclass())) {
    return ctx->thread->raiseTypeErrorWithCStr(cannot_catch_msg);
  }

  return Bool::fromBool(givenExceptionMatches(ctx->thread, left, right));
}

// opcode 107
bool Interpreter::doCompareOp(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Object right(&scope, ctx->frame->popValue());
  Object left(&scope, ctx->frame->popValue());
  CompareOp op = static_cast<CompareOp>(arg);
  RawObject result;
  if (op == IS) {
    result = Bool::fromBool(*left == *right);
  } else if (op == IS_NOT) {
    result = Bool::fromBool(*left != *right);
  } else if (op == IN) {
    result = sequenceContains(ctx->thread, ctx->frame, left, right);
  } else if (op == NOT_IN) {
    result =
        Bool::negate(sequenceContains(ctx->thread, ctx->frame, left, right));
  } else if (op == EXC_MATCH) {
    result = excMatch(ctx, left, right);
  } else {
    result = compareOperation(ctx->thread, ctx->frame, op, left, right);
  }

  if (result.isError()) return unwind(ctx);
  ctx->frame->pushValue(result);
  return false;
}

// opcode 108
bool Interpreter::doImportName(Context* ctx, word arg) {
  HandleScope scope;
  Code code(&scope, ctx->frame->code());
  Object name(&scope, RawTuple::cast(code.names())->at(arg));
  ctx->frame->popValue();  // from list
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  Object result(&scope, runtime->importModule(name));
  if (result.isError()) return unwind(ctx);
  ctx->frame->setTopValue(*result);
  return false;
}

// opcode 109
bool Interpreter::doImportFrom(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Code code(&scope, ctx->frame->code());
  Object name(&scope, RawTuple::cast(code.names())->at(arg));
  CHECK(name.isStr(), "name not found");
  Module module(&scope, ctx->frame->topValue());
  Runtime* runtime = thread->runtime();
  CHECK(module.isModule(), "Unexpected type to import from");
  RawObject value = runtime->moduleAt(module, name);
  if (value.isError()) {
    thread->raiseWithCStr(LayoutId::kImportError, "cannot import name");
    return unwind(ctx);
  }
  ctx->frame->pushValue(value);
  return false;
}

// opcode 110
void Interpreter::doJumpForward(Context* ctx, word arg) { ctx->pc += arg; }

// opcode 111
void Interpreter::doJumpIfFalseOrPop(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Object self(&scope, ctx->frame->topValue());
  RawObject result = isTrue(ctx->thread, ctx->frame, self);
  if (result == Bool::falseObj()) {
    ctx->pc = arg;
  } else {
    ctx->frame->popValue();
  }
}

// opcode 112
void Interpreter::doJumpIfTrueOrPop(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Object self(&scope, ctx->frame->topValue());
  RawObject result = isTrue(ctx->thread, ctx->frame, self);
  if (result == Bool::trueObj()) {
    ctx->pc = arg;
  } else {
    ctx->frame->popValue();
  }
}

// opcode 113
void Interpreter::doJumpAbsolute(Context* ctx, word arg) { ctx->pc = arg; }

// opcode 114
void Interpreter::doPopJumpIfFalse(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Object self(&scope, ctx->frame->topValue());
  RawObject result = isTrue(ctx->thread, ctx->frame, self);
  ctx->frame->popValue();
  if (result == Bool::falseObj()) {
    ctx->pc = arg;
  }
}

// opcode 115
void Interpreter::doPopJumpIfTrue(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Object self(&scope, ctx->frame->topValue());
  RawObject result = isTrue(ctx->thread, ctx->frame, self);
  ctx->frame->popValue();
  if (result == Bool::trueObj()) {
    ctx->pc = arg;
  }
}

// opcode 116
void Interpreter::doLoadGlobal(Context* ctx, word arg) {
  RawObject value =
      RawValueCell::cast(RawTuple::cast(ctx->frame->fastGlobals())->at(arg))
          ->value();
  if (value->isValueCell()) {
    CHECK(
        !RawValueCell::cast(value)->isUnbound(), "Unbound global '%s'",
        RawStr::cast(
            RawTuple::cast(RawCode::cast(ctx->frame->code())->names())->at(arg))
            ->toCStr());
    value = RawValueCell::cast(value)->value();
  }
  ctx->frame->pushValue(value);
  DCHECK(ctx->frame->topValue() != Error::object(), "unexpected error object");
}

// opcode 119
void Interpreter::doContinueLoop(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Object arg_int(&scope, RawSmallInt::fromWord(arg));
  handleLoopExit(ctx, TryBlock::Why::kContinue, arg_int);
}

// opcode 120
void Interpreter::doSetupLoop(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  word stack_depth = frame->valueStackBase() - frame->valueStackTop();
  BlockStack* block_stack = frame->blockStack();
  block_stack->push(TryBlock(TryBlock::kLoop, ctx->pc + arg, stack_depth));
}

// opcode 121
void Interpreter::doSetupExcept(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  word stack_depth = frame->valueStackSize();
  BlockStack* block_stack = frame->blockStack();
  block_stack->push(TryBlock(TryBlock::kExcept, ctx->pc + arg, stack_depth));
}

// opcode 122
void Interpreter::doSetupFinally(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  word stack_depth = frame->valueStackBase() - frame->valueStackTop();
  BlockStack* block_stack = frame->blockStack();
  block_stack->push(TryBlock(TryBlock::kFinally, ctx->pc + arg, stack_depth));
}

// opcode 124
bool Interpreter::doLoadFast(Context* ctx, word arg) {
  // TODO(cshapiro): Need to handle unbound local error
  RawObject value = ctx->frame->local(arg);
  if (value->isError()) {
    Thread* thread = ctx->thread;
    HandleScope scope(thread);
    Str name(
        &scope,
        RawTuple::cast(RawCode::cast(ctx->frame->code())->varnames())->at(arg));
    Str msg(&scope, thread->runtime()->newStrFromFormat(
                        "local variable '%.200s' referenced before assignment",
                        unique_c_ptr<char[]>(name.toCStr()).get()));
    thread->raise(LayoutId::kUnboundLocalError, *msg);
    return unwind(ctx);
  }
  ctx->frame->pushValue(ctx->frame->local(arg));
  return false;
}

// opcode 125
void Interpreter::doStoreFast(Context* ctx, word arg) {
  RawObject value = ctx->frame->popValue();
  ctx->frame->setLocal(arg, value);
}

// opcode 126
void Interpreter::doDeleteFast(Context* ctx, word arg) {
  // TODO(T32821785): use another immediate value than Error to signal unbound
  // local
  if (ctx->frame->local(arg) == Error::object()) {
    RawObject name =
        RawTuple::cast(RawCode::cast(ctx->frame->code())->varnames())->at(arg);
    UNIMPLEMENTED("unbound local %s", RawStr::cast(name)->toCStr());
  }
  ctx->frame->setLocal(arg, Error::object());
}

// opcode 127
void Interpreter::doStoreAnnotation(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Runtime* runtime = ctx->thread->runtime();
  Object names(&scope, RawCode::cast(ctx->frame->code())->names());
  Object value(&scope, ctx->frame->popValue());
  Object key(&scope, RawTuple::cast(*names)->at(arg));

  Dict implicit_globals(&scope, ctx->frame->implicitGlobals());
  Object annotations(&scope,
                     runtime->symbols()->at(SymbolId::kDunderAnnotations));
  Object value_cell(&scope, runtime->dictAt(implicit_globals, annotations));
  Dict anno_dict(&scope, RawValueCell::cast(*value_cell)->value());
  runtime->dictAtPut(anno_dict, key, value);
}

// opcode 130
bool Interpreter::doRaiseVarargs(Context* ctx, word arg) {
  DCHECK(arg >= 0, "Negative argument to RAISE_VARARGS");
  DCHECK(arg <= 2, "Argument to RAISE_VARARGS too large");

  Thread* thread = ctx->thread;
  if (arg == 0) {
    // Re-raise the caught exception.
    if (thread->hasCaughtException()) {
      thread->setPendingExceptionType(thread->caughtExceptionType());
      thread->setPendingExceptionValue(thread->caughtExceptionValue());
      thread->setPendingExceptionTraceback(thread->caughtExceptionTraceback());
    } else {
      thread->raiseRuntimeErrorWithCStr("No active exception to reraise");
    }
  } else {
    Frame* frame = ctx->frame;
    RawObject cause = (arg >= 2) ? frame->popValue() : Error::object();
    RawObject exn = (arg >= 1) ? frame->popValue() : NoneType::object();
    raise(ctx, exn, cause);
  }

  return unwind(ctx);
}

// opcode 131
bool Interpreter::doCallFunction(Context* ctx, word arg) {
  RawObject result = call(ctx->thread, ctx->frame, arg);
  if (result.isError()) return unwind(ctx);
  ctx->frame->pushValue(result);
  return false;
}

// opcode 132
void Interpreter::doMakeFunction(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  HandleScope scope;
  Runtime* runtime = thread->runtime();
  Function function(&scope, runtime->newFunction());
  function.setQualname(frame->popValue());
  function.setCode(frame->popValue());
  function.setName(RawCode::cast(function.code())->name());
  Dict globals(&scope, frame->globals());
  function.setGlobals(*globals);
  Object name_key(&scope, runtime->symbols()->at(SymbolId::kDunderName));
  Object value_cell(&scope, runtime->dictAt(globals, name_key));
  if (value_cell.isValueCell()) {
    DCHECK(!RawValueCell::cast(*value_cell)->isUnbound(), "unbound globals");
    function.setModule(RawValueCell::cast(*value_cell)->value());
  }
  Dict builtins(&scope, frame->builtins());
  Code code(&scope, function.code());
  Object consts_obj(&scope, code.consts());
  if (consts_obj.isTuple() && RawTuple::cast(*consts_obj)->length() >= 1) {
    Tuple consts(&scope, *consts_obj);
    if (consts.at(0)->isStr()) {
      function.setDoc(consts.at(0));
    }
  }
  function.setFastGlobals(runtime->computeFastGlobals(code, globals, builtins));
  if (code.hasCoroutineOrGenerator()) {
    if (code.hasFreevarsOrCellvars()) {
      function.setEntry(generatorClosureTrampoline);
      function.setEntryKw(generatorClosureTrampolineKw);
      function.setEntryEx(generatorClosureTrampolineEx);
    } else {
      function.setEntry(generatorTrampoline);
      function.setEntryKw(generatorTrampolineKw);
      function.setEntryEx(generatorTrampolineEx);
    }
  } else {
    if (code.hasFreevarsOrCellvars()) {
      function.setEntry(interpreterClosureTrampoline);
      function.setEntryKw(interpreterClosureTrampolineKw);
      function.setEntryEx(interpreterClosureTrampolineEx);
    } else {
      function.setEntry(interpreterTrampoline);
      function.setEntryKw(interpreterTrampolineKw);
      function.setEntryEx(interpreterTrampolineEx);
    }
  }
  if (arg & MakeFunctionFlag::CLOSURE) {
    DCHECK((frame->topValue())->isTuple(), "Closure expects tuple");
    function.setClosure(frame->popValue());
  }
  if (arg & MakeFunctionFlag::ANNOTATION_DICT) {
    DCHECK((frame->topValue())->isDict(), "Parameter annotations expect dict");
    function.setAnnotations(frame->popValue());
  }
  if (arg & MakeFunctionFlag::DEFAULT_KW) {
    DCHECK((frame->topValue())->isDict(), "Keyword arguments expect dict");
    function.setKwDefaults(frame->popValue());
  }
  if (arg & MakeFunctionFlag::DEFAULT) {
    DCHECK((frame->topValue())->isTuple(), "Default arguments expect tuple");
    function.setDefaults(frame->popValue());
  }
  frame->pushValue(*function);
}

// opcode 133
void Interpreter::doBuildSlice(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Slice slice(&scope, thread->runtime()->newSlice());
  if (arg == 3) slice.setStep(ctx->frame->popValue());
  slice.setStop(ctx->frame->popValue());
  slice.setStart(ctx->frame->topValue());  // TOP
  ctx->frame->setTopValue(*slice);
}

// opcode 135
void Interpreter::doLoadClosure(Context* ctx, word arg) {
  RawCode code = RawCode::cast(ctx->frame->code());
  ctx->frame->pushValue(ctx->frame->local(code->nlocals() + arg));
}

static RawObject raiseUnboundCellFreeVar(Thread* thread, const Code& code,
                                         word idx) {
  HandleScope scope(thread);
  Object names_obj(&scope, NoneType::object());
  const char* fmt;
  if (idx < code.numCellvars()) {
    names_obj = code.cellvars();
    fmt = "local variable '%.200s' referenced before assignment";
  } else {
    idx -= code.numCellvars();
    names_obj = code.freevars();
    fmt =
        "free variable '%.200s' referenced before assignment in enclosing "
        "scope";
  }
  Tuple names(&scope, *names_obj);
  Str name(&scope, names.at(idx));
  return thread->raise(LayoutId::kUnboundLocalError,
                       thread->runtime()->newStrFromFormat(
                           fmt, unique_c_ptr<char[]>(name.toCStr()).get()));
}

// opcode 136
bool Interpreter::doLoadDeref(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  Code code(&scope, ctx->frame->code());
  ValueCell value(&scope, ctx->frame->local(code.nlocals() + arg));
  if (value.isUnbound()) {
    raiseUnboundCellFreeVar(thread, code, arg);
    return unwind(ctx);
  }
  ctx->frame->pushValue(value.value());
  return false;
}

// opcode 137
void Interpreter::doStoreDeref(Context* ctx, word arg) {
  RawCode code = RawCode::cast(ctx->frame->code());
  RawValueCell::cast(ctx->frame->local(code->nlocals() + arg))
      ->setValue(ctx->frame->popValue());
}

// opcode 138
void Interpreter::doDeleteDeref(Context* ctx, word arg) {
  RawCode code = RawCode::cast(ctx->frame->code());
  RawValueCell::cast(ctx->frame->local(code->nlocals() + arg))->makeUnbound();
}

// opcode 141
bool Interpreter::doCallFunctionKw(Context* ctx, word arg) {
  RawObject result = callKw(ctx->thread, ctx->frame, arg);
  if (result.isError()) return unwind(ctx);
  ctx->frame->pushValue(result);
  return false;
}

// opcode 142
bool Interpreter::doCallFunctionEx(Context* ctx, word arg) {
  RawObject result = callEx(ctx->thread, ctx->frame, arg);
  if (result.isError()) return unwind(ctx);
  ctx->frame->pushValue(result);
  return false;
}

// opcode 143
void Interpreter::doSetupWith(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  Frame* frame = ctx->frame;
  Object mgr(&scope, frame->topValue());
  Object exit_selector(&scope, runtime->symbols()->DunderExit());
  Object enter(&scope,
               lookupMethod(thread, frame, mgr, SymbolId::kDunderEnter));
  BoundMethod exit(&scope, runtime->attributeAt(thread, mgr, exit_selector));
  frame->setTopValue(*exit);
  Object result(&scope, callMethod1(thread, frame, enter, mgr));

  word stack_depth = frame->valueStackBase() - frame->valueStackTop();
  BlockStack* block_stack = frame->blockStack();
  block_stack->push(TryBlock(TryBlock::kFinally, ctx->pc + arg, stack_depth));
  frame->pushValue(*result);
}

// opcode 145
void Interpreter::doListAppend(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Object value(&scope, ctx->frame->popValue());
  List list(&scope, ctx->frame->peek(arg - 1));
  ctx->thread->runtime()->listAdd(list, value);
}

// opcode 146
void Interpreter::doSetAdd(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Object value(&scope, ctx->frame->popValue());
  Set set(&scope, RawSet::cast(ctx->frame->peek(arg - 1)));
  ctx->thread->runtime()->setAdd(set, value);
}

// opcode 147
void Interpreter::doMapAdd(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Object key(&scope, ctx->frame->popValue());
  Object value(&scope, ctx->frame->popValue());
  Dict dict(&scope, RawDict::cast(ctx->frame->peek(arg - 1)));
  ctx->thread->runtime()->dictAtPut(dict, key, value);
}

// opcode 148
void Interpreter::doLoadClassDeref(Context* ctx, word arg) {
  HandleScope scope(ctx->thread);
  Code code(&scope, ctx->frame->code());
  word idx = arg - code.numCellvars();
  Object name(&scope, RawTuple::cast(code.freevars())->at(idx));
  Dict implicit_global(&scope, ctx->frame->implicitGlobals());
  Object result(&scope, ctx->thread->runtime()->dictAt(implicit_global, name));
  if (result.isError()) {
    ValueCell value_cell(&scope, ctx->frame->local(code.nlocals() + arg));
    if (value_cell.isUnbound()) {
      UNIMPLEMENTED("unbound free var %s", RawStr::cast(*name)->toCStr());
    }
    ctx->frame->pushValue(value_cell.value());
  } else {
    ctx->frame->pushValue(RawValueCell::cast(*result)->value());
  }
}

// opcode 149
bool Interpreter::doBuildListUnpack(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  List list(&scope, runtime->newList());
  Object obj(&scope, NoneType::object());
  for (word i = arg - 1; i >= 0; i--) {
    obj = frame->peek(i);
    if (listExtend(thread, list, obj).isError()) return unwind(ctx);
  }
  frame->dropValues(arg - 1);
  frame->setTopValue(*list);
  return false;
}

// opcode 150
bool Interpreter::doBuildMapUnpack(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  Dict dict(&scope, runtime->newDict());
  Object obj(&scope, NoneType::object());
  for (word i = arg - 1; i >= 0; i--) {
    obj = frame->peek(i);
    if (dictMergeOverride(thread, dict, obj).isError()) {
      if (thread->pendingExceptionType() ==
          runtime->typeAt(LayoutId::kAttributeError)) {
        // TODO(bsimmers): Include type name once we have a better formatter.
        thread->clearPendingException();
        thread->raiseTypeErrorWithCStr("object is not a mapping");
      }
      return unwind(ctx);
    }
  }
  frame->dropValues(arg - 1);
  frame->setTopValue(*dict);
  return false;
}

// opcode 151
bool Interpreter::doBuildMapUnpackWithCall(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  Dict dict(&scope, runtime->newDict());
  Object obj(&scope, NoneType::object());
  for (word i = arg - 1; i >= 0; i--) {
    obj = frame->peek(i);
    if (dictMergeError(thread, dict, obj).isError()) {
      if (thread->pendingExceptionType() ==
          runtime->typeAt(LayoutId::kAttributeError)) {
        thread->clearPendingException();
        thread->raiseTypeErrorWithCStr("object is not a mapping");
      } else if (thread->pendingExceptionType() ==
                 runtime->typeAt(LayoutId::kKeyError)) {
        Object value(&scope, thread->pendingExceptionValue());
        thread->clearPendingException();
        // TODO(bsimmers): Make these error messages more informative once
        // we have a better formatter.
        if (runtime->isInstanceOfStr(*value)) {
          thread->raiseTypeErrorWithCStr(
              "got multiple values for keyword argument");
        } else {
          thread->raiseTypeErrorWithCStr("keywords must be strings");
        }
      }
      return unwind(ctx);
    }
  }
  frame->dropValues(arg - 1);
  frame->setTopValue(*dict);
  return false;
}

// opcode 152 & opcode 158
bool Interpreter::doBuildTupleUnpack(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  List list(&scope, runtime->newList());
  Object obj(&scope, NoneType::object());
  for (word i = arg - 1; i >= 0; i--) {
    obj = frame->peek(i);
    if (listExtend(thread, list, obj).isError()) return unwind(ctx);
  }
  Tuple tuple(&scope, runtime->newTuple(list.numItems()));
  for (word i = 0; i < list.numItems(); i++) {
    tuple.atPut(i, list.at(i));
  }
  frame->dropValues(arg - 1);
  frame->setTopValue(*tuple);
  return false;
}

// opcode 153
bool Interpreter::doBuildSetUnpack(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  Set set(&scope, runtime->newSet());
  Object obj(&scope, NoneType::object());
  for (word i = 0; i < arg; i++) {
    obj = frame->peek(i);
    if (runtime->setUpdate(thread, set, obj).isError()) return unwind(ctx);
  }
  frame->dropValues(arg - 1);
  frame->setTopValue(*set);
  return false;
}

// opcode 154
void Interpreter::doSetupAsyncWith(Context* ctx, word arg) {
  Frame* frame = ctx->frame;
  HandleScope scope(ctx->thread);
  Object result(&scope, frame->popValue());
  word stack_depth = frame->valueStackSize();
  BlockStack* block_stack = frame->blockStack();
  block_stack->push(TryBlock(TryBlock::kFinally, ctx->pc + arg, stack_depth));
  frame->pushValue(*result);
}

// opcode 155
bool Interpreter::doFormatValue(Context* ctx, word flags) {
  Thread* thread = ctx->thread;
  HandleScope scope(thread);
  int conv = (flags & FVC_MASK_FLAG);
  int have_fmt_spec = (flags & FVS_MASK_FLAG) == FVS_HAVE_SPEC_FLAG;
  Runtime* runtime = thread->runtime();
  Object fmt_spec(&scope, runtime->newStrFromCStr(""));
  if (have_fmt_spec) {
    fmt_spec = ctx->frame->popValue();
  }
  Object value(&scope, ctx->frame->popValue());
  Object method(&scope, NoneType::object());
  Frame* frame = ctx->frame;
  switch (conv) {
    case FVC_STR_FLAG: {
      method = lookupMethod(thread, frame, value, SymbolId::kDunderStr);
      CHECK(!method.isError(),
            "__str__ doesn't exist for this object, which is impossible since "
            "object has a __str__, and everything descends from object");
      value = callMethod1(thread, frame, method, value);
      if (value.isError()) {
        return unwind(ctx);
      }
      if (!runtime->isInstanceOfStr(*value)) {
        thread->raiseTypeErrorWithCStr("__str__ returned non-string");
        return unwind(ctx);
      }
      break;
    }
    case FVC_REPR_FLAG: {
      method = lookupMethod(thread, frame, value, SymbolId::kDunderRepr);
      CHECK(!method.isError(),
            "__repr__ doesn't exist for this object, which is impossible since "
            "object has a __repr__, and everything descends from object");
      value = callMethod1(thread, frame, method, value);
      if (value.isError()) {
        return unwind(ctx);
      }
      if (!runtime->isInstanceOfStr(*value)) {
        thread->raiseTypeErrorWithCStr("__repr__ returned non-string");
        return unwind(ctx);
      }
      break;
    }
    case FVC_ASCII_FLAG: {
      method = lookupMethod(thread, frame, value, SymbolId::kDunderRepr);
      CHECK(!method.isError(),
            "__repr__ doesn't exist for this object, which is impossible since "
            "object has a __repr__, and everything descends from object");
      value = callMethod1(thread, frame, method, value);
      if (value.isError()) {
        return unwind(ctx);
      }
      if (!runtime->isInstanceOfStr(*value)) {
        thread->raiseTypeErrorWithCStr("__repr__ returned non-string");
        return unwind(ctx);
      }
      value = strEscapeNonASCII(thread, value);
      break;
    }
    default:  // 0: no conv
      break;
  }
  method = lookupMethod(thread, frame, value, SymbolId::kDunderFormat);
  if (method.isError()) {
    return unwind(ctx);
  }
  value = callMethod2(thread, frame, method, value, fmt_spec);
  if (value.isError()) {
    return unwind(ctx);
  }
  if (!runtime->isInstanceOfStr(*value)) {
    thread->raiseTypeErrorWithCStr("__format__ returned non-string");
    return unwind(ctx);
  }
  ctx->frame->pushValue(*value);
  return false;
}

// opcode 156
void Interpreter::doBuildConstKeyMap(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  HandleScope scope;
  Tuple keys(&scope, ctx->frame->popValue());
  Dict dict(&scope, thread->runtime()->newDictWithSize(keys.length()));
  for (word i = arg - 1; i >= 0; i--) {
    Object key(&scope, keys.at(i));
    Object value(&scope, ctx->frame->popValue());
    thread->runtime()->dictAtPut(dict, key, value);
  }
  ctx->frame->pushValue(*dict);
}

// opcode 157
void Interpreter::doBuildString(Context* ctx, word arg) {
  Thread* thread = ctx->thread;
  Runtime* runtime = thread->runtime();
  switch (arg) {
    case 0:  // empty
      ctx->frame->pushValue(runtime->newStrWithAll(View<byte>(nullptr, 0)));
      break;
    case 1:  // no-op
      break;
    default: {  // concat
      RawObject res = stringJoin(thread, ctx->frame->valueStackTop(), arg);
      ctx->frame->dropValues(arg - 1);
      ctx->frame->setTopValue(res);
      break;
    }
  }
}

// Bytecode handlers that might raise an exception return bool rather than
// void.  These wrappers normalize the calling convention so handlers that
// never raise can continue returning void.  They will likely be rendered
// obsolete by future work we have planned to restructure and/or JIT the
// interpreter dispatch loop.
using VoidOp = void (*)(Interpreter::Context*, word);
using BoolOp = bool (*)(Interpreter::Context*, word);

template <VoidOp op>
bool wrapHandler(Interpreter::Context* ctx, word arg) {
  op(ctx, arg);
  return false;
}

template <BoolOp op>
bool wrapHandler(Interpreter::Context* ctx, word arg) {
  return op(ctx, arg);
}

const BoolOp kOpTable[] = {
#define HANDLER(name, value, handler) wrapHandler<Interpreter::handler>,
    FOREACH_BYTECODE(HANDLER)
#undef HANDLER
};

RawObject Interpreter::execute(Thread* thread, Frame* frame) {
  HandleScope scope(thread);
  Code code(&scope, frame->code());
  Bytes byte_array(&scope, code.code());
  Context ctx;
  ctx.pc = frame->virtualPC();
  ctx.thread = thread;
  ctx.frame = frame;
  for (;;) {
    frame->setVirtualPC(ctx.pc);
    Bytecode bc = static_cast<Bytecode>(byte_array.byteAt(ctx.pc++));
    int32 arg = byte_array.byteAt(ctx.pc++);
    while (bc == Bytecode::EXTENDED_ARG) {
      bc = static_cast<Bytecode>(byte_array.byteAt(ctx.pc++));
      arg = (arg << 8) | byte_array.byteAt(ctx.pc++);
    }

    if (kOpTable[bc](&ctx, arg)) {
      RawObject return_val = frame->popValue();
      thread->popFrame();
      return return_val;
    }
  }
}

}  // namespace python
