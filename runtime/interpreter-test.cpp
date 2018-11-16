#include "gtest/gtest.h"

#include "bytecode.h"
#include "frame.h"
#include "handles.h"
#include "interpreter.h"
#include "objects.h"
#include "runtime.h"
#include "trampolines.h"

#include "test-utils.h"

namespace python {

TEST(InterpreterTest, IsTrueBool) {
  Runtime runtime;
  HandleScope scope;

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  ASSERT_TRUE(frame->isSentinelFrame());

  Handle<Object> true_value(&scope, Bool::trueObj());
  frame->pushValue(*true_value);
  EXPECT_EQ(Interpreter::isTrue(thread, frame), Bool::trueObj());

  Handle<Object> false_object(&scope, Bool::falseObj());
  frame->pushValue(*false_object);
  EXPECT_EQ(Interpreter::isTrue(thread, frame), Bool::falseObj());
}

TEST(InterpreterTest, IsTrueInt) {
  Runtime runtime;
  HandleScope scope;

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  ASSERT_TRUE(frame->isSentinelFrame());

  Handle<Object> true_value(&scope, runtime.newInt(1234));
  frame->pushValue(*true_value);
  EXPECT_EQ(Interpreter::isTrue(thread, frame), Bool::trueObj());

  Handle<Object> false_value(&scope, runtime.newInt(0));
  frame->pushValue(*false_value);
  EXPECT_EQ(Interpreter::isTrue(thread, frame), Bool::falseObj());
}

TEST(InterpreterTest, IsTrueDunderLen) {
  Runtime runtime;
  HandleScope scope;

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  ASSERT_TRUE(frame->isSentinelFrame());

  Handle<List> nonempty_list(&scope, runtime.newList());
  Handle<Object> elt(&scope, None::object());
  runtime.listAdd(nonempty_list, elt);

  Handle<Object> true_value(&scope, *nonempty_list);
  frame->pushValue(*true_value);
  EXPECT_EQ(Interpreter::isTrue(thread, frame), Bool::trueObj());

  Handle<List> empty_list(&scope, runtime.newList());
  Handle<Object> false_value(&scope, *empty_list);
  frame->pushValue(*false_value);
  EXPECT_EQ(Interpreter::isTrue(thread, frame), Bool::falseObj());
}

TEST(InterpreterTest, BinaryOpInvokesSelfMethod) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
class C:
    def __sub__(self, other):
        return (C, '__sub__', self, other)

left = C()
right = C()
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> left(&scope, testing::moduleAt(&runtime, main, "left"));
  Handle<Object> right(&scope, testing::moduleAt(&runtime, main, "right"));
  Handle<Object> c_class(&scope, testing::moduleAt(&runtime, main, "C"));

  Object* result = Interpreter::binaryOperation(
      thread, frame, Interpreter::BinaryOp::SUB, left, right);
  ASSERT_TRUE(result->isObjectArray());
  ASSERT_EQ(ObjectArray::cast(result)->length(), 4);
  EXPECT_EQ(ObjectArray::cast(result)->at(0), *c_class);
  ASSERT_TRUE(ObjectArray::cast(result)->at(1)->isString());
  String* name = String::cast(ObjectArray::cast(result)->at(1));
  EXPECT_TRUE(name->equalsCString("__sub__"));
  EXPECT_EQ(ObjectArray::cast(result)->at(2), *left);
  EXPECT_EQ(ObjectArray::cast(result)->at(3), *right);
}

TEST(InterpreterTest, BinaryOpInvokesSelfMethodIgnoresReflectedMethod) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
class C:
    def __sub__(self, other):
        return (C, '__sub__', self, other)
    def __rsub__(self, other):
        return (C, '__rsub__', self, other)

left = C()
right = C()
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> left(&scope, testing::moduleAt(&runtime, main, "left"));
  Handle<Object> right(&scope, testing::moduleAt(&runtime, main, "right"));
  Handle<Object> c_class(&scope, testing::moduleAt(&runtime, main, "C"));

  Object* result = Interpreter::binaryOperation(
      thread, frame, Interpreter::BinaryOp::SUB, left, right);
  ASSERT_TRUE(result->isObjectArray());
  ASSERT_EQ(ObjectArray::cast(result)->length(), 4);
  EXPECT_EQ(ObjectArray::cast(result)->at(0), *c_class);
  ASSERT_TRUE(ObjectArray::cast(result)->at(1)->isString());
  String* name = String::cast(ObjectArray::cast(result)->at(1));
  EXPECT_TRUE(name->equalsCString("__sub__"));
  EXPECT_EQ(ObjectArray::cast(result)->at(2), *left);
  EXPECT_EQ(ObjectArray::cast(result)->at(3), *right);
}

TEST(InterpreterTest, BinaryOperationInvokesSubclassReflectedMethod) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
class C:
    def __sub__(self, other):
        return (C, '__sub__', self, other)

class D(C):
    def __rsub__(self, other):
        return (D, '__rsub__', self, other)

left = C()
right = D()
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> left(&scope, testing::moduleAt(&runtime, main, "left"));
  Handle<Object> right(&scope, testing::moduleAt(&runtime, main, "right"));
  Handle<Object> d_class(&scope, testing::moduleAt(&runtime, main, "D"));

  Object* result = Interpreter::binaryOperation(
      thread, frame, Interpreter::BinaryOp::SUB, left, right);
  ASSERT_TRUE(result->isObjectArray());
  ASSERT_EQ(ObjectArray::cast(result)->length(), 4);
  EXPECT_EQ(ObjectArray::cast(result)->at(0), *d_class);
  EXPECT_TRUE(String::cast(ObjectArray::cast(result)->at(1))
                  ->equalsCString("__rsub__"));
  EXPECT_EQ(ObjectArray::cast(result)->at(2), *right);
  EXPECT_EQ(ObjectArray::cast(result)->at(3), *left);
}

TEST(InterpreterTest, BinaryOperationInvokesOtherReflectedMethod) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
class C:
    pass

class D:
    def __rsub__(self, other):
        return (D, '__rsub__', self, other)

left = C()
right = D()
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> left(&scope, testing::moduleAt(&runtime, main, "left"));
  Handle<Object> right(&scope, testing::moduleAt(&runtime, main, "right"));
  Handle<Object> d_class(&scope, testing::moduleAt(&runtime, main, "D"));

  Object* result = Interpreter::binaryOperation(
      thread, frame, Interpreter::BinaryOp::SUB, left, right);
  ASSERT_TRUE(result->isObjectArray());
  ASSERT_EQ(ObjectArray::cast(result)->length(), 4);
  EXPECT_EQ(ObjectArray::cast(result)->at(0), *d_class);
  EXPECT_TRUE(String::cast(ObjectArray::cast(result)->at(1))
                  ->equalsCString("__rsub__"));
  EXPECT_EQ(ObjectArray::cast(result)->at(2), *right);
  EXPECT_EQ(ObjectArray::cast(result)->at(3), *left);
}

TEST(InterpreterTest, InplaceOperationCallsInplaceMethod) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
class C:
    def __isub__(self, other):
        return (C, '__isub__', self, other)

left = C()
right = C()
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> left(&scope, testing::moduleAt(&runtime, main, "left"));
  Handle<Object> right(&scope, testing::moduleAt(&runtime, main, "right"));
  Handle<Object> c_class(&scope, testing::moduleAt(&runtime, main, "C"));

  Object* result = Interpreter::inplaceOperation(
      thread, frame, Interpreter::BinaryOp::SUB, left, right);
  ASSERT_TRUE(result->isObjectArray());
  ASSERT_EQ(ObjectArray::cast(result)->length(), 4);
  EXPECT_EQ(ObjectArray::cast(result)->at(0), *c_class);
  EXPECT_TRUE(String::cast(ObjectArray::cast(result)->at(1))
                  ->equalsCString("__isub__"));
  EXPECT_EQ(ObjectArray::cast(result)->at(2), *left);
  EXPECT_EQ(ObjectArray::cast(result)->at(3), *right);
}

TEST(InterpreterTest, InplaceOperationCallsBinaryMethod) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
class C:
    def __sub__(self, other):
        return (C, '__sub__', self, other)

left = C()
right = C()
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> left(&scope, testing::moduleAt(&runtime, main, "left"));
  Handle<Object> right(&scope, testing::moduleAt(&runtime, main, "right"));
  Handle<Object> c_class(&scope, testing::moduleAt(&runtime, main, "C"));

  Object* result = Interpreter::inplaceOperation(
      thread, frame, Interpreter::BinaryOp::SUB, left, right);
  ASSERT_TRUE(result->isObjectArray());
  ASSERT_EQ(ObjectArray::cast(result)->length(), 4);
  EXPECT_EQ(ObjectArray::cast(result)->at(0), *c_class);
  EXPECT_TRUE(
      String::cast(ObjectArray::cast(result)->at(1))->equalsCString("__sub__"));
  EXPECT_EQ(ObjectArray::cast(result)->at(2), *left);
  EXPECT_EQ(ObjectArray::cast(result)->at(3), *right);
}

TEST(InterpreterTest, InplaceOperationCallsBinaryMethodAfterNotImplemented) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
class C:
    def __isub__(self, other):
        return NotImplemented
    def __sub__(self, other):
        return (C, '__sub__', self, other)

left = C()
right = C()
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> left(&scope, testing::moduleAt(&runtime, main, "left"));
  Handle<Object> right(&scope, testing::moduleAt(&runtime, main, "right"));
  Handle<Object> c_class(&scope, testing::moduleAt(&runtime, main, "C"));

  Object* result = Interpreter::inplaceOperation(
      thread, frame, Interpreter::BinaryOp::SUB, left, right);
  ASSERT_TRUE(result->isObjectArray());
  ASSERT_EQ(ObjectArray::cast(result)->length(), 4);
  EXPECT_EQ(ObjectArray::cast(result)->at(0), *c_class);
  EXPECT_TRUE(
      String::cast(ObjectArray::cast(result)->at(1))->equalsCString("__sub__"));
  EXPECT_EQ(ObjectArray::cast(result)->at(2), *left);
  EXPECT_EQ(ObjectArray::cast(result)->at(3), *right);
}

// To a rich comparison on two instances of the same type.  In each case, the
// method on the left side of the comparison should be used.
TEST(InterpreterTest, CompareOpSameClass) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
class C:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return self.value < other.value

c10 = C(10)
c20 = C(20)
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  ASSERT_TRUE(frame->isSentinelFrame());

  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> left(&scope, testing::moduleAt(&runtime, main, "c10"));
  Handle<Object> right(&scope, testing::moduleAt(&runtime, main, "c20"));

  Object* left_lt_right =
      Interpreter::compareOperation(thread, frame, CompareOp::LT, left, right);
  EXPECT_EQ(left_lt_right, Bool::trueObj());

  Object* right_lt_left =
      Interpreter::compareOperation(thread, frame, CompareOp::LT, right, left);
  EXPECT_EQ(right_lt_left, Bool::falseObj());
}

TEST(InterpreterTest, CompareOpFallback) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
class C:
    def __init__(self, value):
        self.value = value

c10 = C(10)
c20 = C(20)
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  ASSERT_TRUE(frame->isSentinelFrame());

  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> left(&scope, testing::moduleAt(&runtime, main, "c10"));
  Handle<Object> right(&scope, testing::moduleAt(&runtime, main, "c20"));

  Object* left_eq_right =
      Interpreter::compareOperation(thread, frame, CompareOp::EQ, left, right);
  EXPECT_EQ(left_eq_right, Bool::falseObj());
  Object* left_ne_right =
      Interpreter::compareOperation(thread, frame, CompareOp::NE, left, right);
  EXPECT_EQ(left_ne_right, Bool::trueObj());

  Object* right_eq_left =
      Interpreter::compareOperation(thread, frame, CompareOp::EQ, left, right);
  EXPECT_EQ(right_eq_left, Bool::falseObj());
  Object* right_ne_left =
      Interpreter::compareOperation(thread, frame, CompareOp::NE, left, right);
  EXPECT_EQ(right_ne_left, Bool::trueObj());
}

TEST(InterpreterTest, SequenceContains) {
  Runtime runtime;
  HandleScope scope;

  runtime.runFromCString(R"(
a = {1, 2}

b = 1
c = 3
)");

  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();

  ASSERT_TRUE(frame->isSentinelFrame());
  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> container(&scope, testing::moduleAt(&runtime, main, "a"));
  Handle<Object> b(&scope, testing::moduleAt(&runtime, main, "b"));
  Handle<Object> c(&scope, testing::moduleAt(&runtime, main, "c"));
  Object* contains_true =
      Interpreter::sequenceContains(thread, frame, b, container);
  Object* contains_false =
      Interpreter::sequenceContains(thread, frame, c, container);
  EXPECT_EQ(contains_true, Bool::trueObj());
  EXPECT_EQ(contains_false, Bool::falseObj());
}

TEST(InterpreterTest, ContextManagerCallEnterExit) {
  const char* src = R"(
a = 1
class Foo:
  def __enter__(self):
    global a
    a = 2

  def __exit__(self, e, t, b):
    global a
    a = 3

b = 0
with Foo():
  b = a

)";
  Runtime runtime;
  HandleScope scope;
  runtime.runFromCString(src);
  Handle<Module> main(&scope, testing::findModule(&runtime, "__main__"));
  Handle<Object> a(&scope, testing::moduleAt(&runtime, main, "a"));
  EXPECT_EQ(SmallInt::cast(*a)->value(), 3);
  Handle<Object> b(&scope, testing::moduleAt(&runtime, main, "b"));
  EXPECT_EQ(SmallInt::cast(*b)->value(), 2);
}

TEST(InterpreterTest, StackCleanupAfterCallFunction) {
  // Build the following function
  //    def foo(a=1, b=2):
  //      return 42
  //
  // Then call as foo(1) and verify that the stack is cleaned up after
  // default argument expansion
  //
  Runtime runtime;
  HandleScope scope;
  Thread* thread = Thread::currentThread();

  Handle<Code> code(&scope, runtime.newCode());

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(1));
  consts->atPut(0, SmallInt::fromWord(42));
  code->setConsts(*consts);

  Handle<ObjectArray> names(&scope, runtime.newObjectArray(1));
  Handle<Object> key(&scope, runtime.newStringFromCString("foo"));
  names->atPut(0, *key);
  code->setNames(*names);
  code->setArgcount(2);
  code->setStacksize(1);

  const byte bytecode[] = {LOAD_CONST, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode));

  Handle<Function> callee(&scope, runtime.newFunction());
  callee->setCode(*code);
  callee->setEntry(interpreterTrampoline);
  Handle<ObjectArray> defaults(&scope, runtime.newObjectArray(2));

  defaults->atPut(0, SmallInt::fromWord(1));
  defaults->atPut(1, SmallInt::fromWord(2));
  callee->setDefaults(*defaults);

  // Create a caller frame
  Frame* frame = thread->pushFrame(*code);

  // Save starting value stack top
  Object** value_stack_start = frame->valueStackTop();

  // Push function pointer and argument
  frame->pushValue(*callee);
  frame->pushValue(SmallInt::fromWord(1));

  Object* result = Interpreter::call(thread, frame, 1);

  // Make sure we got the right result and stack is back where it should be
  EXPECT_EQ(SmallInt::cast(result)->value(), 42);
  EXPECT_EQ(value_stack_start, frame->valueStackTop());
}

TEST(InterpreterTest, StackCleanupAfterCallExFunction) {
  // Build the following function
  //    def foo(a=1, b=2):
  //      return 42
  //
  // Then call as "f=(2,); foo(*f)" and verify that the stack is cleaned up
  // after ex and default argument expansion
  //
  Runtime runtime;
  HandleScope scope;
  Thread* thread = Thread::currentThread();

  Handle<Code> code(&scope, runtime.newCode());

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(1));
  consts->atPut(0, SmallInt::fromWord(42));
  code->setConsts(*consts);

  Handle<ObjectArray> names(&scope, runtime.newObjectArray(1));
  Handle<Object> key(&scope, runtime.newStringFromCString("foo"));
  names->atPut(0, *key);
  code->setNames(*names);
  code->setArgcount(2);
  code->setStacksize(1);

  const byte bytecode[] = {LOAD_CONST, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode));

  Handle<Function> callee(&scope, runtime.newFunction());
  callee->setCode(*code);
  callee->setEntryEx(interpreterTrampolineEx);
  Handle<ObjectArray> defaults(&scope, runtime.newObjectArray(2));

  defaults->atPut(0, SmallInt::fromWord(1));
  defaults->atPut(1, SmallInt::fromWord(2));
  callee->setDefaults(*defaults);

  // Create a caller frame
  Frame* frame = thread->pushFrame(*code);

  // Save starting value stack top
  Object** value_stack_start = frame->valueStackTop();

  // Push function pointer and argument
  Handle<ObjectArray> ex(&scope, runtime.newObjectArray(1));
  ex->atPut(0, SmallInt::fromWord(2));
  frame->pushValue(*callee);
  frame->pushValue(*ex);

  Object* result = Interpreter::callEx(thread, frame, 0);

  // Make sure we got the right result and stack is back where it should be
  EXPECT_EQ(SmallInt::cast(result)->value(), 42);
  EXPECT_EQ(value_stack_start, frame->valueStackTop());
}

TEST(InterpreterTest, StackCleanupAfterCallKwFunction) {
  // Build the following function
  //    def foo(a=1, b=2):
  //      return 42
  //
  // Then call as "foo(b=4)" and verify that the stack is cleaned up after
  // ex and default argument expansion
  //
  Runtime runtime;
  HandleScope scope;
  Thread* thread = Thread::currentThread();

  Handle<Code> code(&scope, runtime.newCode());

  Handle<ObjectArray> consts(&scope, runtime.newObjectArray(1));
  consts->atPut(0, SmallInt::fromWord(42));
  code->setConsts(*consts);

  Handle<ObjectArray> names(&scope, runtime.newObjectArray(1));
  Handle<Object> key(&scope, runtime.newStringFromCString("foo"));
  names->atPut(0, *key);
  code->setNames(*names);
  code->setArgcount(2);
  code->setStacksize(1);
  Handle<ObjectArray> var_names(&scope, runtime.newObjectArray(2));
  var_names->atPut(0, runtime.newStringFromCString("a"));
  var_names->atPut(1, runtime.newStringFromCString("b"));
  code->setVarnames(*var_names);

  const byte bytecode[] = {LOAD_CONST, 0, RETURN_VALUE, 0};
  code->setCode(runtime.newByteArrayWithAll(bytecode));

  Handle<Function> callee(&scope, runtime.newFunction());
  callee->setCode(*code);
  callee->setEntryKw(interpreterTrampolineKw);
  Handle<ObjectArray> defaults(&scope, runtime.newObjectArray(2));

  defaults->atPut(0, SmallInt::fromWord(1));
  defaults->atPut(1, SmallInt::fromWord(2));
  callee->setDefaults(*defaults);

  // Create a caller frame
  Frame* frame = thread->pushFrame(*code);

  // Save starting value stack top
  Object** value_stack_start = frame->valueStackTop();

  // Push function pointer and argument
  Handle<ObjectArray> arg_names(&scope, runtime.newObjectArray(1));
  arg_names->atPut(0, runtime.newStringFromCString("b"));
  frame->pushValue(*callee);
  frame->pushValue(SmallInt::fromWord(4));
  frame->pushValue(*arg_names);

  Object* result = Interpreter::callKw(thread, frame, 1);

  // Make sure we got the right result and stack is back where it should be
  EXPECT_EQ(SmallInt::cast(result)->value(), 42);
  EXPECT_EQ(value_stack_start, frame->valueStackTop());
}

}  // namespace python
