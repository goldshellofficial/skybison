// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "under-collections-module.h"

#include "gtest/gtest.h"

#include "builtins-module.h"
#include "builtins.h"
#include "objects.h"
#include "runtime.h"
#include "test-utils.h"

namespace py {

namespace testing {

using UnderCollectionsModuleTest = RuntimeFixture;

TEST_F(UnderCollectionsModuleTest, DunderNewConstructsDeque) {
  HandleScope scope(thread_);
  Type type(&scope, runtime_->typeAt(LayoutId::kDeque));
  Object result(&scope, runBuiltin(METH(deque, __new__), type));
  ASSERT_TRUE(result.isDeque());
  Deque deque(&scope, *result);
  EXPECT_EQ(deque.left(), 0);
  EXPECT_EQ(deque.numItems(), 0);
  EXPECT_EQ(deque.capacity(), 0);
  EXPECT_EQ(deque.items(), SmallInt::fromWord(0));
}

TEST_F(UnderCollectionsModuleTest, DequeAppendInsertsElementToEnd) {
  HandleScope scope(thread_);
  Deque self(&scope, runtime_->newDeque());
  // Test underlying array growth
  for (int i = 0; i < 30; i++) {
    Object value(&scope, SmallInt::fromWord(i));
    Object result(&scope, runBuiltin(METH(deque, append), self, value));
    EXPECT_EQ(*result, NoneType::object());
  }

  EXPECT_EQ(self.numItems(), 30);
  for (int i = 0; i < 30; i++) {
    EXPECT_TRUE(isIntEqualsWord(self.at(i), i)) << i;
  }
}

TEST_F(UnderCollectionsModuleTest, DequeAppendAfterAppendleftResizesCorrectly) {
  HandleScope scope(thread_);
  Deque self(&scope, runtime_->newDeque());
  // Test underlying array growth
  Object value(&scope, SmallInt::fromWord(0));
  Object result(&scope, runBuiltin(METH(deque, appendleft), self, value));
  EXPECT_EQ(*result, NoneType::object());
  for (int i = 1; i < 30; i++) {
    value = SmallInt::fromWord(i);
    result = runBuiltin(METH(deque, append), self, value);
    EXPECT_EQ(*result, NoneType::object());
  }

  EXPECT_EQ(self.numItems(), 30);
  for (int i = 0; i < 30; i++) {
    EXPECT_TRUE(isIntEqualsWord(self.at(i), i)) << i;
  }
}

TEST_F(UnderCollectionsModuleTest, DequeAppendleftInsertsElementToFront) {
  HandleScope scope(thread_);
  Deque self(&scope, runtime_->newDeque());
  Object value(&scope, SmallInt::fromWord(1));
  Object value1(&scope, SmallInt::fromWord(2));
  runBuiltin(METH(deque, appendleft), self, value);
  Object result(&scope, runBuiltin(METH(deque, appendleft), self, value1));
  EXPECT_EQ(*result, NoneType::object());

  EXPECT_EQ(self.numItems(), 2);
  EXPECT_TRUE(isIntEqualsWord(self.at(self.capacity() - 1), 1));
  EXPECT_TRUE(isIntEqualsWord(self.at(self.capacity() - 2), 2));
}

TEST_F(UnderCollectionsModuleTest, DequeClearRemovesElements) {
  HandleScope scope(thread_);
  Deque self(&scope, runtime_->newDeque());
  Object value(&scope, SmallInt::fromWord(0));
  Object value1(&scope, SmallInt::fromWord(1));
  Object value2(&scope, SmallInt::fromWord(2));
  runBuiltin(METH(deque, append), self, value);
  runBuiltin(METH(deque, append), self, value1);
  runBuiltin(METH(deque, append), self, value2);
  Object result(&scope, runBuiltin(METH(deque, clear), self));

  EXPECT_EQ(*result, NoneType::object());
  ASSERT_EQ(self.numItems(), 0);
  EXPECT_EQ(self.at(0), NoneType::object());
  EXPECT_EQ(self.at(1), NoneType::object());
  EXPECT_EQ(self.at(2), NoneType::object());
}

TEST_F(UnderCollectionsModuleTest, DequePopRemovesItemFromRight) {
  HandleScope scope(thread_);
  Deque deque(&scope, runtime_->newDeque());
  for (int i = 0; i < 3; i++) {
    Object value(&scope, SmallInt::fromWord(i));
    runBuiltin(METH(deque, append), deque, value);
  }
  ASSERT_EQ(deque.numItems(), 3);

  // Pop from the end
  RawObject result = runBuiltin(METH(deque, pop), deque);
  ASSERT_EQ(deque.numItems(), 2);
  EXPECT_TRUE(isIntEqualsWord(deque.at(1), 1));
  EXPECT_TRUE(isIntEqualsWord(result, 2));
}

TEST_F(UnderCollectionsModuleTest, DequePopLeftRemovesItemFromLeft) {
  HandleScope scope(thread_);
  Deque deque(&scope, runtime_->newDeque());
  for (int i = 0; i < 3; i++) {
    Object value(&scope, SmallInt::fromWord(i));
    runBuiltin(METH(deque, append), deque, value);
  }
  ASSERT_EQ(deque.numItems(), 3);

  // Pop from the front
  RawObject result = runBuiltin(METH(deque, popleft), deque);
  ASSERT_EQ(deque.numItems(), 2);
  EXPECT_TRUE(isIntEqualsWord(deque.at(2), 2));
  EXPECT_TRUE(isIntEqualsWord(result, 0));
}

TEST_F(UnderCollectionsModuleTest, DequePopLeftAtEndRemovesItemFromLefts) {
  HandleScope scope(thread_);
  Deque deque(&scope, runtime_->newDeque());
  Object value(&scope, SmallInt::fromWord(0));
  runBuiltin(METH(deque, appendleft), deque, value);
  ASSERT_EQ(deque.numItems(), 1);

  // Pop from the front
  RawObject result = runBuiltin(METH(deque, popleft), deque);
  ASSERT_EQ(deque.numItems(), 0);
  EXPECT_EQ(deque.left(), 0);
  EXPECT_EQ(deque.capacity(), 16);
  EXPECT_TRUE(isIntEqualsWord(result, 0));
}

TEST_F(UnderCollectionsModuleTest, EmptyDequeInvariants) {
  HandleScope scope(thread_);
  Deque deque(&scope, runtime_->newDeque());
  EXPECT_EQ(deque.left(), 0);
  EXPECT_EQ(deque.numItems(), 0);
  EXPECT_EQ(deque.capacity(), 0);
  EXPECT_EQ(deque.items(), SmallInt::fromWord(0));
}

}  // namespace testing

}  // namespace py
