#include "gtest/gtest.h"

#include "frame.h"
#include "objects.h"
#include "range-builtins.h"
#include "runtime.h"
#include "test-utils.h"

namespace python {

using namespace testing;

TEST(RangeBuiltinsTest, DunderIterReturnsRangeIter) {
  Runtime runtime;
  HandleScope scope;
  Object empty_range(&scope, runtime.newRange(0, 0, 1));
  Object iter(&scope, runBuiltin(RangeBuiltins::dunderIter, empty_range));
  ASSERT_TRUE(iter.isRangeIterator());
}

TEST(RangeBuiltinsTest, CallDunderNext) {
  Runtime runtime;
  HandleScope scope;
  Object range(&scope, runtime.newRange(0, 2, 1));
  Object iter(&scope, runBuiltin(RangeBuiltins::dunderIter, range));
  ASSERT_TRUE(iter.isRangeIterator());

  Object item1(&scope, runBuiltin(RangeIteratorBuiltins::dunderNext, iter));
  EXPECT_TRUE(isIntEqualsWord(*item1, 0));

  Object item2(&scope, runBuiltin(RangeIteratorBuiltins::dunderNext, iter));
  EXPECT_TRUE(isIntEqualsWord(*item2, 1));
}

TEST(RangeIteratorBuiltinsTest, DunderIterReturnsSelf) {
  Runtime runtime;
  HandleScope scope;
  Object empty_range(&scope, runtime.newRange(0, 0, 1));
  Object iter(&scope, runBuiltin(RangeBuiltins::dunderIter, empty_range));
  ASSERT_TRUE(iter.isRangeIterator());

  // Now call __iter__ on the iterator object
  Object result(&scope, runBuiltin(RangeIteratorBuiltins::dunderIter, iter));
  ASSERT_EQ(*result, *iter);
}

TEST(RangeIteratorBuiltinsTest, DunderLengthHintReturnsPendingLength) {
  Runtime runtime;
  HandleScope scope;
  Object empty_range(&scope, runtime.newRange(0, 0, 1));
  Object iter(&scope, runBuiltin(RangeBuiltins::dunderIter, empty_range));
  ASSERT_TRUE(iter.isRangeIterator());

  Object length_hint1(
      &scope, runBuiltin(RangeIteratorBuiltins::dunderLengthHint, iter));
  EXPECT_TRUE(isIntEqualsWord(*length_hint1, 0));

  RawRangeIterator::cast(*iter).setRange(runtime.newRange(0, 1, 1));
  Object length_hint2(
      &scope, runBuiltin(RangeIteratorBuiltins::dunderLengthHint, iter));
  EXPECT_TRUE(isIntEqualsWord(*length_hint2, 1));

  // Consume the iterator
  Object item1(&scope, runBuiltin(RangeIteratorBuiltins::dunderNext, iter));
  EXPECT_TRUE(isIntEqualsWord(*item1, 0));

  Object length_hint3(
      &scope, runBuiltin(RangeIteratorBuiltins::dunderLengthHint, iter));
  EXPECT_TRUE(isIntEqualsWord(*length_hint3, 0));
}

TEST(RangeIteratorBuiltinsTest, DunderLengthHintWithNegativeStepRange) {
  Runtime runtime;
  HandleScope scope;
  Object neg_range(&scope, runtime.newRange(0, -2, -1));
  Object iter(&scope, runBuiltin(RangeBuiltins::dunderIter, neg_range));
  ASSERT_TRUE(iter.isRangeIterator());

  Object length_hint1(
      &scope, runBuiltin(RangeIteratorBuiltins::dunderLengthHint, iter));
  EXPECT_TRUE(isIntEqualsWord(*length_hint1, 2));

  // Consume the iterator
  Object item1(&scope, runBuiltin(RangeIteratorBuiltins::dunderNext, iter));
  EXPECT_TRUE(isIntEqualsWord(*item1, 0));

  Object length_hint2(
      &scope, runBuiltin(RangeIteratorBuiltins::dunderLengthHint, iter));
  EXPECT_TRUE(isIntEqualsWord(*length_hint2, 1));
}

}  // namespace python
