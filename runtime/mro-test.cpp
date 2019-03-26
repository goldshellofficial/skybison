#include "gtest/gtest.h"

#include "mro.h"
#include "test-utils.h"

namespace python {
using namespace testing;

TEST(MroTest, ComputeMroReturnsList) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class A: pass
)")
                   .isError());
  Object a_obj(&scope, moduleAt(&runtime, "__main__", "A"));
  Type a(&scope, *a_obj);
  Tuple parents(&scope, runtime.newTuple(0));
  Object result_obj(&scope, computeMro(thread, a, parents));
  ASSERT_TRUE(result_obj.isTuple());
  Tuple result(&scope, *result_obj);
  ASSERT_EQ(result.length(), 2);
  EXPECT_EQ(result.at(0), a);
  EXPECT_EQ(result.at(1), runtime.typeAt(LayoutId::kObject));
}

TEST(MroTest, ComputeMroWithTypeSubclassReturnsList) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class Meta(type): pass
class A(metaclass=Meta): pass
class B(A): pass
)")
                   .isError());
  Object a_obj(&scope, moduleAt(&runtime, "__main__", "A"));
  Object b_obj(&scope, moduleAt(&runtime, "__main__", "B"));
  Type b(&scope, *b_obj);
  Tuple parents(&scope, runtime.newTuple(1));
  parents.atPut(0, *a_obj);
  Object result_obj(&scope, computeMro(thread, b, parents));
  Tuple result(&scope, *result_obj);
  ASSERT_EQ(result.length(), 3);
  EXPECT_EQ(result.at(0), b);
  EXPECT_EQ(result.at(1), a_obj);
  EXPECT_EQ(result.at(2), runtime.typeAt(LayoutId::kObject));
}

TEST(MroTest, ComputeMroWithMultipleTypeSubclassesReturnsList) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class Meta(type): pass
class A(metaclass=Meta): pass
class B(metaclass=Meta): pass
class C(A, B): pass
)")
                   .isError());
  Object a_obj(&scope, moduleAt(&runtime, "__main__", "A"));
  Object b_obj(&scope, moduleAt(&runtime, "__main__", "B"));
  Object c_obj(&scope, moduleAt(&runtime, "__main__", "C"));
  Type c(&scope, *c_obj);
  Tuple parents(&scope, runtime.newTuple(2));
  parents.atPut(0, *a_obj);
  parents.atPut(1, *b_obj);
  Object result_obj(&scope, computeMro(thread, c, parents));
  Tuple result(&scope, *result_obj);
  ASSERT_EQ(result.length(), 4);
  EXPECT_EQ(result.at(0), c);
  EXPECT_EQ(result.at(1), a_obj);
  EXPECT_EQ(result.at(2), b_obj);
  EXPECT_EQ(result.at(3), runtime.typeAt(LayoutId::kObject));
}

}  // namespace python
