#include "gtest/gtest.h"

#include <iostream>
#include <sstream>

#include "builtins.h"
#include "bytecode.h"
#include "frame.h"
#include "globals.h"
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

TEST(ThreadTest, RunHelloWorld) {
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

  // TODO(cshapiro): abstract away retrieving the main module.
  Handle<Dictionary> modules(&scope, runtime.modules());
  Handle<Object> key(&scope, runtime.newStringFromCString("__main__"));
  Handle<Object> value(&scope, None::object());
  bool is_present = runtime.dictionaryAt(modules, key, value.pointer());
  ASSERT_TRUE(is_present);
  Handle<Module> main(&scope, *value);

  Object* result = Thread::currentThread()->runModuleFunction(*main, code);
  ASSERT_EQ(result, None::object()); // returns None
}

TEST(ThreadTest, ModuleBodyCallsHelloWorldFunction) {
  Runtime runtime;
  HandleScope scope;

  // These bytes correspond to the .pyc output from compiling a file containing
  // just the following statements:
  //
  //   def hello():
  //     print('hello, world')
  //   hello()
  const char* buffer =
      "\x33\x0D\x0D\x0A\x20\x05\x1E\x5A\x50\x00\x00\x00\xE3\x00\x00\x00\x00\x00"
      "\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x40\x00\x00\x00\x73\x12\x00"
      "\x00\x00\x64\x00\x64\x01\x84\x00\x5A\x00\x65\x00\x83\x00\x01\x00\x64\x02"
      "\x53\x00\x29\x03\x63\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02"
      "\x00\x00\x00\x43\x00\x00\x00\x73\x0C\x00\x00\x00\x74\x00\x64\x01\x83\x01"
      "\x01\x00\x64\x00\x53\x00\x29\x02\x4E\x7A\x0C\x68\x65\x6C\x6C\x6F\x2C\x20"
      "\x77\x6F\x72\x6C\x64\x29\x01\xDA\x05\x70\x72\x69\x6E\x74\xA9\x00\x72\x02"
      "\x00\x00\x00\x72\x02\x00\x00\x00\xFA\x0C\x63\x61\x6C\x6C\x68\x65\x6C\x6C"
      "\x6F\x2E\x70\x79\xDA\x0A\x68\x65\x6C\x6C\x6F\x77\x6F\x72\x6C\x64\x02\x00"
      "\x00\x00\x73\x02\x00\x00\x00\x00\x01\x72\x04\x00\x00\x00\x4E\x29\x01\x72"
      "\x04\x00\x00\x00\x72\x02\x00\x00\x00\x72\x02\x00\x00\x00\x72\x02\x00\x00"
      "\x00\x72\x03\x00\x00\x00\xDA\x08\x3C\x6D\x6F\x64\x75\x6C\x65\x3E\x02\x00"
      "\x00\x00\x73\x02\x00\x00\x00\x08\x02";
  Marshal::Reader reader(&scope, &runtime, buffer);

  int32 magic = reader.readLong();
  EXPECT_EQ(magic, 0x0A0D0D33);
  int32 mtime = reader.readLong();
  EXPECT_EQ(mtime, 0x5A1E0520);
  int32 size = reader.readLong();
  EXPECT_EQ(size, 80);

  auto code = reader.readObject();
  ASSERT_TRUE(code->isCode());
  EXPECT_EQ(Code::cast(code)->argcount(), 0);

  // TODO(cshapiro): abstract away retrieving the main module.
  Handle<Dictionary> modules(&scope, runtime.modules());
  Handle<Object> key(&scope, runtime.newStringFromCString("__main__"));
  Handle<Object> value(&scope, None::object());
  bool is_present = runtime.dictionaryAt(modules, key, value.pointer());
  ASSERT_TRUE(is_present);
  Handle<Module> main(&scope, *value);

  Object* result = Thread::currentThread()->runModuleFunction(*main, code);
  ASSERT_EQ(result, None::object()); // returns None
}

TEST(ThreadTest, OverlappingFrames) {
  Runtime runtime;
  HandleScope scope;

  // Push a frame for a code object with space for 3 items on the value stack
  Handle<Code> callerCode(&scope, runtime.newCode());
  callerCode->setStacksize(3);
  auto thread = Thread::currentThread();
  auto callerFrame = thread->pushFrame(*callerCode, thread->initialFrame());
  Object** sp = callerFrame->valueStackTop();
  // Push args on the stack in the sequence generated by CPython
  auto arg1 = SmallInteger::fromWord(1111);
  *--sp = arg1;
  auto arg2 = SmallInteger::fromWord(2222);
  *--sp = arg2;
  auto arg3 = SmallInteger::fromWord(3333);
  *--sp = arg3;
  callerFrame->setValueStackTop(sp);

  // Push a frame for a code object that expects 3 arguments and needs space
  // for 3 local variables
  Handle<Code> code(&scope, runtime.newCode());
  code->setArgcount(3);
  code->setNlocals(6);
  auto frame = thread->pushFrame(*code, callerFrame);

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
  byte* prevSp = thread->ptr();
  auto frame = thread->pushFrame(*code, thread->initialFrame());

  // Verify frame invariants post-push
  EXPECT_EQ(frame->previousFrame(), thread->initialFrame());
  EXPECT_EQ(frame->code(), *code);
  EXPECT_EQ(frame->valueStackTop(), reinterpret_cast<Object**>(frame));
  EXPECT_EQ(frame->base(), frame->valueStackTop());
  EXPECT_EQ(
      frame->locals() + code->nlocals(), reinterpret_cast<Object**>(prevSp));
  EXPECT_EQ(frame->previousSp(), prevSp);

  // Make sure we restore the thread's stack pointer back to its previous
  // location
  thread->popFrame(frame);
  EXPECT_EQ(thread->ptr(), prevSp);
}

TEST(ThreadTest, ManipulateValueStack) {
  Runtime runtime;
  HandleScope scope;
  auto thread = Thread::currentThread();
  auto frame = thread->openAndLinkFrame(0, 3, thread->initialFrame());

  // Push 3 items on the value stack
  Object** sp = frame->valueStackTop();
  *--sp = SmallInteger::fromWord(1111);
  *--sp = SmallInteger::fromWord(2222);
  *--sp = SmallInteger::fromWord(3333);
  frame->setValueStackTop(sp);
  ASSERT_EQ(frame->valueStackTop(), sp);

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
  frame->setValueStackTop(sp + 2);
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
  auto expectedResult = SmallInteger::fromWord(2222);
  Handle<Code> calleeCode(&scope, runtime.newCode());
  calleeCode->setArgcount(2);
  calleeCode->setStacksize(1);
  calleeCode->setConsts(runtime.newObjectArray(1));
  ObjectArray::cast(calleeCode->consts())->atPut(0, expectedResult);
  const byte callee_bc[] = {LOAD_CONST, 0, RETURN_VALUE, 0};
  calleeCode->setCode(
      runtime.newByteArrayWithAll(callee_bc, ARRAYSIZE(callee_bc)));

  // Create the function object and bind it to the code object
  Handle<Function> callee(&scope, runtime.newFunction());
  callee->setCode(*calleeCode);
  callee->setEntry(interpreterTrampoline);

  // Build a code object to call the function defined above
  Handle<Code> callerCode(&scope, runtime.newCode());
  callerCode->setStacksize(3);
  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(3));
  consts->atPut(0, *callee);
  consts->atPut(1, SmallInteger::fromWord(1111));
  consts->atPut(2, SmallInteger::fromWord(2222));
  callerCode->setConsts(*consts);
  const byte caller_bc[] = {LOAD_CONST,
                            0,
                            LOAD_CONST,
                            1,
                            LOAD_CONST,
                            2,
                            CALL_FUNCTION,
                            2,
                            RETURN_VALUE,
                            0};
  callerCode->setCode(
      runtime.newByteArrayWithAll(caller_bc, ARRAYSIZE(caller_bc)));

  // Execute the caller and make sure we get back the expected result
  Object* result = Thread::currentThread()->run(*callerCode);
  ASSERT_TRUE(result->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(result)->value(), expectedResult->value());
}

static Object* firstArg(Thread*, Frame* callerFrame, word argc) {
  if (argc == 0) {
    return None::object();
  }
  return *(callerFrame->valueStackTop() + argc - 1);
}

TEST(ThreadTest, CallBuiltinFunction) {
  Runtime runtime;
  HandleScope scope;

  // Create the builtin function
  Handle<Function> callee(&scope, runtime.newFunction());
  callee->setEntry(firstArg);

  // Set up a code object that calls the builtin with a single argument.
  Handle<Code> code(&scope, runtime.newCode());
  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(2));
  consts->atPut(0, *callee);
  consts->atPut(1, SmallInteger::fromWord(1111));
  code->setConsts(*consts);
  const byte bytecode[] = {
      LOAD_CONST, 0, LOAD_CONST, 1, CALL_FUNCTION, 1, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));
  code->setStacksize(2);

  // Execute the code and make sure we get back the result we expect
  Object* result = Thread::currentThread()->run(*code);
  ASSERT_TRUE(result->isSmallInteger());
  ASSERT_EQ(SmallInteger::cast(result)->value(), 1111);
}

TEST(ThreadTest, CallBuiltinPrint) {
  Runtime runtime;
  HandleScope scope;

  // Create the builtin function
  Handle<Function> callee(&scope, runtime.newFunction());
  callee->setEntry(builtinPrint);

  Handle<Code> code(&scope, runtime.newCode());
  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(5));
  consts->atPut(0, *callee);
  consts->atPut(1, SmallInteger::fromWord(1111));
  consts->atPut(2, runtime.newStringFromCString("testing 123"));
  consts->atPut(3, Boolean::fromBool(true));
  consts->atPut(4, Boolean::fromBool(false));
  code->setConsts(*consts);
  const byte bytecode[] = {LOAD_CONST,
                           0,
                           LOAD_CONST,
                           1,
                           LOAD_CONST,
                           2,
                           LOAD_CONST,
                           3,
                           LOAD_CONST,
                           4,
                           CALL_FUNCTION,
                           4,
                           RETURN_VALUE,
                           0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));
  code->setStacksize(5);

  std::stringstream stream;
  std::ostream* oldStream = builtinPrintStream;
  builtinPrintStream = &stream;

  // Execute the code and make sure we get back the result we expect
  Thread::currentThread()->run(*code);
  builtinPrintStream = oldStream;

  EXPECT_STREQ(stream.str().c_str(), "1111 testing 123 True False\n");
}

TEST(ThreadTest, ExecuteDupTop) {
  Runtime runtime;
  HandleScope scope;

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(1));
  consts->atPut(0, SmallInteger::fromWord(1111));
  Handle<Code> code(&scope, runtime.newCode());
  code->setStacksize(2);
  code->setConsts(*consts);
  const byte bytecode[] = {LOAD_CONST, 0, DUP_TOP, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));

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
  const byte bytecode[] = {
      LOAD_CONST, 0, LOAD_CONST, 1, ROT_TWO, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));

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
  const byte bytecode[] = {
      JUMP_ABSOLUTE, 4, LOAD_CONST, 0, LOAD_CONST, 1, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));

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
  const byte bytecode[] = {
      JUMP_FORWARD, 2, LOAD_CONST, 0, LOAD_CONST, 1, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));

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
  const byte bytecode[] = {
      LOAD_CONST, 0, STORE_FAST, 1, LOAD_FAST, 1, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));

  Object* result = Thread::currentThread()->run(*code);
  ASSERT_TRUE(result->isSmallInteger());
  EXPECT_EQ(SmallInteger::cast(result)->value(), 1111);
}

TEST(ThreadTest, LoadGlobal) {
  Runtime runtime;
  HandleScope scope;

  Handle<Code> code(&scope, runtime.newCode());
  Handle<ObjectArray> names(&scope, runtime.newObjectArray(1));
  Handle<Object> key(&scope, runtime.newStringFromCString("foo"));
  names->atPut(0, *key);
  code->setNames(*names);

  const byte bytecode[] = {LOAD_GLOBAL, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->pushFrame(*code, thread->initialFrame());

  Handle<Dictionary> globals(&scope, runtime.newDictionary());
  Handle<ValueCell> value_cell(&scope, runtime.newValueCell());
  value_cell->setValue(SmallInteger::fromWord(1234));
  Handle<Object> value(&scope, *value_cell);
  runtime.dictionaryAtPut(globals, key, value);
  frame->setGlobals(*globals);

  Handle<Object> result(&scope, Interpreter::execute(thread, frame));
  EXPECT_EQ(*result, value_cell->value());
}

TEST(ThreadTest, StoreGlobalCreateValueCell) {
  Runtime runtime;
  HandleScope scope;

  Handle<Code> code(&scope, runtime.newCode());

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(1));
  consts->atPut(0, SmallInteger::fromWord(42));
  code->setConsts(*consts);

  Handle<ObjectArray> names(&scope, runtime.newObjectArray(1));
  Handle<Object> key(&scope, runtime.newStringFromCString("foo"));
  names->atPut(0, *key);
  code->setNames(*names);

  const byte bytecode[] = {
      LOAD_CONST, 0, STORE_GLOBAL, 0, LOAD_GLOBAL, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->pushFrame(*code, thread->initialFrame());

  Handle<Dictionary> globals(&scope, runtime.newDictionary());
  frame->setGlobals(*globals);

  Handle<Object> result(&scope, Interpreter::execute(thread, frame));

  Handle<Object> value(&scope, None::object());
  bool is_present = runtime.dictionaryAt(globals, key, value.pointer());
  ASSERT_TRUE(is_present);
  Handle<ValueCell> value_cell(&scope, *value);
  EXPECT_EQ(*result, value_cell->value());
}

TEST(ThreadTest, StoreGlobalReuseValueCell) {
  Runtime runtime;
  HandleScope scope;

  Handle<Code> code(&scope, runtime.newCode());

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(1));
  consts->atPut(0, SmallInteger::fromWord(42));
  code->setConsts(*consts);

  Handle<ObjectArray> names(&scope, runtime.newObjectArray(1));
  Handle<Object> key(&scope, runtime.newStringFromCString("foo"));
  names->atPut(0, *key);
  code->setNames(*names);

  const byte bytecode[] = {
      LOAD_CONST, 0, STORE_GLOBAL, 0, LOAD_GLOBAL, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->pushFrame(*code, thread->initialFrame());

  Handle<ValueCell> value_cell1(&scope, runtime.newValueCell());
  value_cell1->setValue(SmallInteger::fromWord(99));

  Handle<Dictionary> globals(&scope, runtime.newDictionary());
  Handle<Object> value(&scope, *value_cell1);
  runtime.dictionaryAtPut(globals, key, value);
  frame->setGlobals(*globals);

  Handle<Object> result(&scope, Interpreter::execute(thread, frame));

  Handle<Object> value_cell2(&scope, None::object());
  bool is_present = runtime.dictionaryAt(globals, key, value_cell2.pointer());
  ASSERT_TRUE(is_present);
  EXPECT_EQ(*value_cell2, *value_cell1);
  EXPECT_EQ(SmallInteger::fromWord(42), value_cell1->value());
}

TEST(ThreadTest, StoreNameCreateValueCell) {
  Runtime runtime;
  HandleScope scope;

  Handle<Code> code(&scope, runtime.newCode());

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(1));
  consts->atPut(0, SmallInteger::fromWord(42));
  code->setConsts(*consts);

  Handle<ObjectArray> names(&scope, runtime.newObjectArray(1));
  Handle<Object> key(&scope, runtime.newStringFromCString("foo"));
  names->atPut(0, *key);
  code->setNames(*names);

  const byte bytecode[] = {
      LOAD_CONST, 0, STORE_NAME, 0, LOAD_NAME, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode, ARRAYSIZE(bytecode)));

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->pushFrame(*code, thread->initialFrame());

  Handle<Dictionary> implicit_globals(&scope, runtime.newDictionary());
  frame->setImplicitGlobals(*implicit_globals);

  Handle<Object> result(&scope, Interpreter::execute(thread, frame));

  Handle<Object> value(&scope, None::object());
  bool is_present =
      runtime.dictionaryAt(implicit_globals, key, value.pointer());
  ASSERT_TRUE(is_present);
  Handle<ValueCell> value_cell(&scope, *value);
  EXPECT_EQ(*result, value_cell->value());
}

TEST(ThreadTest, MakeFunction) {
  Runtime runtime;
  HandleScope scope;

  Handle<Code> module(&scope, runtime.newCode());

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(3));
  consts->atPut(0, runtime.newCode());
  Handle<Object> key(&scope, runtime.newStringFromCString("hello"));
  consts->atPut(1, *key);
  consts->atPut(2, None::object());
  module->setConsts(*consts);

  Handle<ObjectArray> names(&scope, runtime.newObjectArray(1));
  names->atPut(0, runtime.newStringFromCString("hello"));
  module->setNames(*names);

  const byte bc[] = {LOAD_CONST,
                     0,
                     LOAD_CONST,
                     1,
                     MAKE_FUNCTION,
                     0,
                     STORE_NAME,
                     0,
                     LOAD_CONST,
                     2,
                     RETURN_VALUE,
                     0};
  module->setCode(runtime.newByteArrayWithAll(bc, ARRAYSIZE(bc)));

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->pushFrame(*module, thread->initialFrame());

  Handle<Dictionary> implicit_globals(&scope, runtime.newDictionary());
  frame->setImplicitGlobals(*implicit_globals);

  Handle<Object> result(&scope, Interpreter::execute(thread, frame));

  Handle<Object> value(&scope, None::object());
  bool is_present =
      runtime.dictionaryAt(implicit_globals, key, value.pointer());
  ASSERT_TRUE(is_present);
  Handle<ValueCell> value_cell(&scope, *value);
  ASSERT_TRUE(value_cell->value()->isFunction());

  Handle<Function> function(&scope, value_cell->value());
  EXPECT_EQ(function->code(), consts->at(0));
  EXPECT_EQ(function->name(), consts->at(1));
  EXPECT_EQ(function->entry(), &interpreterTrampoline);
}

} // namespace python
