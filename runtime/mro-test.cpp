// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "mro.h"

#include "gtest/gtest.h"

#include "test-utils.h"

namespace py {
namespace testing {

using MroTest = RuntimeFixture;

TEST_F(MroTest, ComputeMroReturnsList) {
  HandleScope scope(thread_);
  ASSERT_FALSE(runFromCStr(runtime_, R"(
class A: pass
)")
                   .isError());
  Object a_obj(&scope, mainModuleAt(runtime_, "A"));
  Type a(&scope, *a_obj);
  a.setBases(runtime_->implicitBases());
  Object result_obj(&scope, computeMro(thread_, a));
  ASSERT_TRUE(result_obj.isTuple());
  Tuple result(&scope, *result_obj);
  ASSERT_EQ(result.length(), 2);
  EXPECT_EQ(result.at(0), a);
  EXPECT_EQ(result.at(1), runtime_->typeAt(LayoutId::kObject));
}

TEST_F(MroTest, ComputeMroWithTypeSubclassReturnsList) {
  HandleScope scope(thread_);
  ASSERT_FALSE(runFromCStr(runtime_, R"(
class Meta(type): pass
class A(metaclass=Meta): pass
class B(A): pass
)")
                   .isError());
  Object a_obj(&scope, mainModuleAt(runtime_, "A"));
  Type b(&scope, mainModuleAt(runtime_, "B"));
  b.setBases(runtime_->newTupleWith1(a_obj));
  Object result_obj(&scope, computeMro(thread_, b));
  Tuple result(&scope, *result_obj);
  ASSERT_EQ(result.length(), 3);
  EXPECT_EQ(result.at(0), b);
  EXPECT_EQ(result.at(1), a_obj);
  EXPECT_EQ(result.at(2), runtime_->typeAt(LayoutId::kObject));
}

TEST_F(MroTest, ComputeMroWithMultipleTypeSubclassesReturnsList) {
  HandleScope scope(thread_);
  ASSERT_FALSE(runFromCStr(runtime_, R"(
class Meta(type): pass
class A(metaclass=Meta): pass
class B(metaclass=Meta): pass
class C(A, B): pass
)")
                   .isError());
  Object a_obj(&scope, mainModuleAt(runtime_, "A"));
  Object b_obj(&scope, mainModuleAt(runtime_, "B"));
  Type c(&scope, mainModuleAt(runtime_, "C"));
  c.setBases(runtime_->newTupleWith2(a_obj, b_obj));
  Object result_obj(&scope, computeMro(thread_, c));
  Tuple result(&scope, *result_obj);
  ASSERT_EQ(result.length(), 4);
  EXPECT_EQ(result.at(0), c);
  EXPECT_EQ(result.at(1), a_obj);
  EXPECT_EQ(result.at(2), b_obj);
  EXPECT_EQ(result.at(3), runtime_->typeAt(LayoutId::kObject));
}

TEST_F(MroTest, ComputeMroWithIncompatibleBasesRaisesTypeError) {
  HandleScope scope(thread_);
  ASSERT_FALSE(runFromCStr(runtime_, R"(
class A: pass
class B(A): pass
)")
                   .isError());
  Object a_obj(&scope, mainModuleAt(runtime_, "A"));
  Object b_obj(&scope, mainModuleAt(runtime_, "B"));
  Type c(&scope, runtime_->newType());
  c.setBases(runtime_->newTupleWith2(a_obj, b_obj));
  EXPECT_TRUE(raisedWithStr(computeMro(thread_, c), LayoutId::kTypeError,
                            "Cannot create a consistent method resolution "
                            "order (MRO) for bases A, B"));
}

}  // namespace testing
}  // namespace py
