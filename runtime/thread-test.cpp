#include "gtest/gtest.h"

#include "bytecode.h"
#include "frame.h"
#include "interpreter.h"
#include "marshal.h"
#include "runtime.h"
#include "thread.h"
#include "trampolines.h"

namespace python {

TEST(ThreadTest, CheckMainThreadRuntime) {
  Runtime runtime;
  auto thread = Thread::currentThread();
  ASSERT_EQ(thread->runtime(), &runtime);
}

TEST(ThreadTest, RunEmptyFunction) {
  Runtime runtime;
  HandleScope scope;
  const char* buffer =
      "\x33\x0D\x0D\x0A\x3B\x5B\xB8\x59\x05\x00\x00\x00\xE3\x00\x00\x00\x00\x00"
      "\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x40\x00\x00\x00\x73\x04\x00"
      "\x00\x00\x64\x00\x53\x00\x29\x01\x4E\xA9\x00\x72\x01\x00\x00\x00\x72\x01"
      "\x00\x00\x00\x72\x01\x00\x00\x00\xFA\x07\x70\x61\x73\x73\x2E\x70\x79\xDA"
      "\x08\x3C\x6D\x6F\x64\x75\x6C\x65\x3E\x01\x00\x00\x00\x73\x00\x00\x00\x00";
  Marshal::Reader reader(&scope, &runtime, buffer);

  int32 magic = reader.readLong();
  EXPECT_EQ(magic, 0x0A0D0D33);
  int32 mtime = reader.readLong();
  EXPECT_EQ(mtime, 0x59B85B3B);
  int32 size = reader.readLong();
  EXPECT_EQ(size, 5);

  auto code = reader.readObject();
  ASSERT_TRUE(code->isCode());
  EXPECT_EQ(Code::cast(code)->argcount(), 0);

  Thread thread(1 * KiB);
  Object* result = thread.run(code);
  ASSERT_EQ(result, None::object()); // returns None
}

TEST(ThreadTest, DISABLED_RunHelloWorld) {
  Runtime runtime;
  HandleScope scope;
  const char* buffer =
      "\x33\x0D\x0D\x0A\x1B\x69\xC1\x59\x16\x00\x00\x00\xE3\x00\x00\x00\x00\x00"
      "\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x40\x00\x00\x00\x73\x0C\x00"
      "\x00\x00\x65\x00\x64\x00\x83\x01\x01\x00\x64\x01\x53\x00\x29\x02\x7A\x0C"
      "\x68\x65\x6C\x6C\x6F\x2C\x20\x77\x6F\x72\x6C\x64\x4E\x29\x01\xDA\x05\x70"
      "\x72\x69\x6E\x74\xA9\x00\x72\x02\x00\x00\x00\x72\x02\x00\x00\x00\xFA\x0D"
      "\x68\x65\x6C\x6C\x6F\x77\x6F\x72\x6C\x64\x2E\x70\x79\xDA\x08\x3C\x6D\x6F"
      "\x64\x75\x6C\x65\x3E\x01\x00\x00\x00\x73\x00\x00\x00\x00";
  Marshal::Reader reader(&scope, &runtime, buffer);

  int32 magic = reader.readLong();
  EXPECT_EQ(magic, 0x0A0D0D33);
  int32 mtime = reader.readLong();
  EXPECT_EQ(mtime, 0x59C1691B);
  int32 size = reader.readLong();
  EXPECT_EQ(size, 22);

  auto code = reader.readObject();
  ASSERT_TRUE(code->isCode());
  EXPECT_EQ(Code::cast(code)->argcount(), 0);

  Thread thread(1 * KiB);
  Object* result = thread.run(code);
  ASSERT_EQ(result, None::object()); // returns None
}

TEST(ThreadTest, OverlappingFrames) {
  Runtime runtime;
  Thread thread(1 * KiB);

  // Push a frame for a code object with space for 3 items on the value stack
  auto callerCode = runtime.newCode();
  Code::cast(callerCode)->setStacksize(3);
  auto callerFrame = thread.pushFrame(callerCode, thread.initialFrame());
  Object** sp = callerFrame->top();
  // Push args on the stack in the sequence generated by CPython
  auto arg1 = SmallInteger::fromWord(1111);
  *--sp = arg1;
  auto arg2 = SmallInteger::fromWord(2222);
  *--sp = arg2;
  auto arg3 = SmallInteger::fromWord(3333);
  *--sp = arg3;
  callerFrame->setTop(sp);

  // Push a frame for a code object that expects 3 arguments and needs space
  // for 3 local variables
  auto code = runtime.newCode();
  Code::cast(code)->setArgcount(3);
  Code::cast(code)->setNlocals(6);
  auto frame = thread.pushFrame(code, callerFrame);

  // Make sure we can read the args from the frame
  Object** locals = frame->locals();

  ASSERT_TRUE(locals[0]->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(locals[3])->value(), arg3->value());

  ASSERT_TRUE(locals[1]->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(locals[4])->value(), arg2->value());

  ASSERT_TRUE(locals[2]->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(locals[5])->value(), arg1->value());
}

TEST(ThreadTest, EncodeTryBlock) {
  TryBlock block(100, 200, 300);
  TryBlock decoded = TryBlock::fromSmallInteger(block.asSmallInteger());
  EXPECT_EQ(decoded.kind(), block.kind());
  EXPECT_EQ(decoded.handler(), block.handler());
  EXPECT_EQ(decoded.level(), block.level());
}

TEST(ThreadTest, PushPopFrame) {
  Runtime runtime;
  HandleScope scope;

  Handle<Code> code(&scope, runtime.newCode());
  code->setNlocals(2);
  code->setStacksize(3);

  auto thread = Thread::currentThread();
  byte* prevLimit = thread->ptr();
  auto frame = thread->pushFrame(*code, thread->initialFrame());

  // Verify frame invariants post-push
  EXPECT_EQ(frame->previousFrame(), thread->initialFrame());
  EXPECT_EQ(frame->code(), *code);
  EXPECT_EQ(frame->top(), reinterpret_cast<Object**>(frame));
  EXPECT_EQ(frame->base(), frame->top());
  EXPECT_EQ(
      frame->locals() + code->nlocals(), reinterpret_cast<Object**>(prevLimit));

  // Make sure we restore the thread's stack pointer back to its previous
  // location
  thread->popFrame(frame);
  EXPECT_EQ(thread->ptr(), prevLimit);
}

TEST(ThreadTest, ManipulateValueStack) {
  Runtime runtime;
  HandleScope scope;

  Handle<Code> code(&scope, runtime.newCode());
  code->setArgcount(2);
  code->setNlocals(2);
  code->setStacksize(3);
  auto thread = Thread::currentThread();
  auto frame = thread->pushFrame(*code, thread->initialFrame());

  // Push 3 items on the value stack
  Object** sp = frame->top();
  *--sp = SmallInteger::fromWord(1111);
  *--sp = SmallInteger::fromWord(2222);
  *--sp = SmallInteger::fromWord(3333);
  frame->setTop(sp);
  ASSERT_EQ(frame->top(), sp);

  // Verify the value stack is laid out as we expect
  word values[] = {3333, 2222, 1111};
  for (int i = 0; i < 3; i++) {
    Object* object = frame->peek(i);
    ASSERT_TRUE(object->isSmallInteger())
        << "Value at stack depth " << i << " is not an integer";
    EXPECT_EQ(SmallInteger::cast(object)->value(), values[i])
        << "Incorrect value at stack depth " << i;
  }

  // Pop 2 items off the stack and check the stack is still as we expect
  frame->setTop(sp + 2);
  Object* top = frame->peek(0);
  ASSERT_TRUE(top->isSmallInteger()) << "Stack top isn't an integer";
  EXPECT_EQ(SmallInteger::cast(top)->value(), 1111)
      << "Incorrect value for stack top";
}

TEST(ThreadTest, CallFunction) {
  Runtime runtime;
  HandleScope scope;

  // Build the code object for the following function
  //
  //     def noop(a, b):
  //         return 2222
  //
  // whose bytecode is
  //
  //     LOAD_CONST    0
  //     RETURN_VALUE
  auto expectedResult = SmallInteger::fromWord(2222);
  Handle<Code> calleeCode(&scope, runtime.newCode());
  calleeCode->setArgcount(2);
  calleeCode->setNlocals(2);
  calleeCode->setConsts(runtime.newObjectArray(1));
  ObjectArray::cast(calleeCode->consts())->atPut(0, expectedResult);
  calleeCode->setCode(runtime.newByteArrayFromCString("\x64\x00\x53\x00", 4));

  // Create the function object and bind it to the code object
  Handle<Function> callee(&scope, runtime.newFunction());
  callee->setCode(*calleeCode);
  callee->setEntry(interpreterTrampoline);

  // Build the code object for the following bytecode snippet
  //
  //     CALL_FUNCTION 2
  //     RETURN_VALUE
  Handle<Code> callerCode(&scope, runtime.newCode());
  callerCode->setCode(runtime.newByteArrayFromCString("\x83\x02\x53\x00", 4));

  // Set up the stack and call the function!
  auto thread = Thread::currentThread();
  auto frame = thread->pushFrame(*callerCode, thread->initialFrame());
  Object** sp = frame->top();
  *--sp = *callee;
  *--sp = SmallInteger::fromWord(100);
  *--sp = SmallInteger::fromWord(200);
  frame->setTop(sp);
  Object* result = Interpreter::execute(thread, frame);

  // Make sure we computed the expected result
  ASSERT_TRUE(result->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(result)->value(), expectedResult->value());
}

TEST(ThreadTest, ExecuteDupTop) {
  Runtime runtime;
  HandleScope scope;

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(1));
  consts->atPut(0, SmallInteger::fromWord(1111));
  Handle<Code> code(&scope, runtime.newCode());
  code->setStacksize(2);
  code->setConsts(*consts);
  const char bytecode[] = {LOAD_CONST, 0, DUP_TOP, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayFromCString(bytecode, ARRAYSIZE(bytecode)));

  Object* result = Thread::currentThread()->run(*code);
  ASSERT_TRUE(result->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(result)->value(), 1111);
}

TEST(ThreadTest, ExecuteRotTwo) {
  Runtime runtime;
  HandleScope scope;

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(2));
  consts->atPut(0, SmallInteger::fromWord(1111));
  consts->atPut(1, SmallInteger::fromWord(2222));
  Handle<Code> code(&scope, runtime.newCode());
  code->setStacksize(2);
  code->setConsts(*consts);
  const char bytecode[] = {
      LOAD_CONST, 0, LOAD_CONST, 1, ROT_TWO, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayFromCString(bytecode, ARRAYSIZE(bytecode)));

  Object* result = Thread::currentThread()->run(*code);
  ASSERT_TRUE(result->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(result)->value(), 1111);
}

TEST(ThreadTest, ExecuteJumpAbsolute) {
  Runtime runtime;
  HandleScope scope;

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(2));
  consts->atPut(0, SmallInteger::fromWord(1111));
  consts->atPut(1, SmallInteger::fromWord(2222));
  Handle<Code> code(&scope, runtime.newCode());
  code->setStacksize(2);
  code->setConsts(*consts);
  const char bytecode[] = {
      JUMP_ABSOLUTE, 4, LOAD_CONST, 0, LOAD_CONST, 1, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayFromCString(bytecode, ARRAYSIZE(bytecode)));

  Object* result = Thread::currentThread()->run(*code);
  ASSERT_TRUE(result->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(result)->value(), 2222);
}

TEST(ThreadTest, ExecuteJumpForward) {
  Runtime runtime;
  HandleScope scope;

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(2));
  consts->atPut(0, SmallInteger::fromWord(1111));
  consts->atPut(1, SmallInteger::fromWord(2222));
  Handle<Code> code(&scope, runtime.newCode());
  code->setStacksize(2);
  code->setConsts(*consts);
  const char bytecode[] = {
      JUMP_FORWARD, 2, LOAD_CONST, 0, LOAD_CONST, 1, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayFromCString(bytecode, ARRAYSIZE(bytecode)));

  Object* result = Thread::currentThread()->run(*code);
  ASSERT_TRUE(result->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(result)->value(), 2222);
}

TEST(ThreadTest, ExecuteStoreLoadFast) {
  Runtime runtime;
  HandleScope scope;

  Handle<Code> code(&scope, runtime.newCode());
  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(1));
  consts->atPut(0, SmallInteger::fromWord(1111));
  code->setConsts(*consts);
  code->setNlocals(2);
  const char bytecode[] = {
      LOAD_CONST, 0, STORE_FAST, 1, LOAD_FAST, 1, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayFromCString(bytecode, ARRAYSIZE(bytecode)));

  Object* result = Thread::currentThread()->run(*code);
  ASSERT_TRUE(result->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(result)->value(), 1111);
}

} // namespace python
