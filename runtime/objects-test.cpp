#include "gtest/gtest.h"

#include <cstdint>

#include "runtime.h"
#include "str-builtins.h"
#include "test-utils.h"

namespace py {

using namespace testing;

using ByteArrayTest = RuntimeFixture;
using LargeBytesTest = RuntimeFixture;
using MutableBytesTest = RuntimeFixture;
using SmallBytesTest = RuntimeFixture;
using CodeTest = RuntimeFixture;
using ComplexTest = RuntimeFixture;
using DoubleTest = RuntimeFixture;
using IntTest = RuntimeFixture;
using LargeStrTest = RuntimeFixture;
using ListTest = RuntimeFixture;
using ModulesTest = RuntimeFixture;
using MutableBytesTest = RuntimeFixture;
using MutableTupleTest = RuntimeFixture;
using SliceTest = RuntimeFixture;
using StrTest = RuntimeFixture;
using StringTest = RuntimeFixture;
using ValueCellTest = RuntimeFixture;
using WeakRefTest = RuntimeFixture;

TEST_F(ByteArrayTest, DownsizeMaintainsCapacity) {
  HandleScope scope(thread_);
  ByteArray array(&scope, runtime_.newByteArray());
  const byte byte_array[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  runtime_.byteArrayExtend(thread_, array, byte_array);
  ASSERT_EQ(array.numItems(), 9);
  word capacity = array.capacity();
  array.downsize(5);
  EXPECT_EQ(array.numItems(), 5);
  EXPECT_EQ(array.capacity(), capacity);
}

TEST_F(SmallBytesTest, CopyToStartAtCopiesToDestinationStartingAtIndex) {
  byte src_bytes[] = "hello";
  HandleScope scope(thread_);
  Bytes src(&scope, runtime_.newBytesWithAll(src_bytes));
  byte result[5] = {0};
  src.copyToStartAt(result, 4, 1);
  EXPECT_STREQ(reinterpret_cast<char*>(result), "ello");
}

TEST_F(LargeBytesTest, CopyToStartAtCopiesToDestinationStartingAtIndex) {
  byte src_bytes[] = "hello world this is patrick";
  HandleScope scope(thread_);
  Bytes src(&scope, runtime_.newBytesWithAll(src_bytes));
  byte result[8] = {0};
  src.copyToStartAt(result, 7, 20);
  EXPECT_STREQ(reinterpret_cast<char*>(result), "patrick");
}

TEST_F(MutableBytesTest, ReplaceFromWithStartAtSelfNoop) {
  HandleScope scope(thread_);
  const byte src_bytes[] = "patrick";
  MutableBytes src(&scope, runtime_.newMutableBytesUninitialized(8));
  for (word i = 0; i < 8; i++) {
    src.byteAtPut(i, src_bytes[i]);
  }
  ASSERT_TRUE(isMutableBytesEqualsBytes(src, src_bytes));
  src.replaceFromWithStartAt(0, Bytes::cast(*src), 3, 0);
  EXPECT_TRUE(isMutableBytesEqualsBytes(src, src_bytes));
}

TEST_F(MutableBytesTest, ReplaceFromWithStartAtSelfBackward) {
  HandleScope scope(thread_);
  const byte src_bytes[] = "patrick";
  MutableBytes src(&scope, runtime_.newMutableBytesUninitialized(8));
  for (word i = 0; i < 8; i++) {
    src.byteAtPut(i, src_bytes[i]);
  }
  ASSERT_TRUE(isMutableBytesEqualsBytes(src, src_bytes));
  src.replaceFromWithStartAt(0, Bytes::cast(*src), 3, 4);
  const byte expected[] = "ickrick";
  EXPECT_TRUE(isMutableBytesEqualsBytes(src, expected));
}

TEST_F(MutableBytesTest, ReplaceFromWithStartAtSelfForward) {
  HandleScope scope(thread_);
  const byte src_bytes[] = "patrick";
  MutableBytes src(&scope, runtime_.newMutableBytesUninitialized(8));
  for (word i = 0; i < 8; i++) {
    src.byteAtPut(i, src_bytes[i]);
  }
  ASSERT_TRUE(isMutableBytesEqualsBytes(src, src_bytes));
  src.replaceFromWithStartAt(4, Bytes::cast(*src), 3, 0);
  const byte expected[] = "patrpat";
  EXPECT_TRUE(isMutableBytesEqualsBytes(src, expected));
}

TEST_F(MutableBytesTest, ReplaceFromWithStartAtReplacesStartingAtSrcIndex) {
  byte src_bytes[] = "hello world this is patrick";
  HandleScope scope(thread_);
  Bytes src(&scope, runtime_.newBytesWithAll(src_bytes));
  MutableBytes dst(&scope, runtime_.newMutableBytesUninitialized(8));
  dst.replaceFromWithStartAt(0, *src, 7, 20);
  const byte expected[] = "patrick";
  EXPECT_TRUE(isMutableBytesEqualsBytes(dst, expected));
}

TEST_F(CodeTest, OffsetToLineNumReturnsLineNumber) {
  const char* src = R"(
def func():
  a = 1
  b = 2
  print(a, b)
)";
  ASSERT_FALSE(runFromCStr(&runtime_, src).isError());
  HandleScope scope(thread_);

  // The bytecode for func is roughly:
  // LOAD_CONST     # a = 1
  // STORE_FAST
  //
  // LOAD_CONST     # b = 2
  // STORE_FAST
  //
  // LOAD_GLOBAL    # print(a, b)
  // LOAD_FAST
  // LOAD_FAST
  // CALL_FUNCTION

  Function func(&scope, mainModuleAt(&runtime_, "func"));
  Code code(&scope, func.code());
  ASSERT_EQ(code.firstlineno(), 2);

  // a = 1
  EXPECT_EQ(code.offsetToLineNum(0), 3);
  EXPECT_EQ(code.offsetToLineNum(2), 3);

  // b = 2
  EXPECT_EQ(code.offsetToLineNum(4), 4);
  EXPECT_EQ(code.offsetToLineNum(6), 4);

  // print(a, b)
  for (word i = 8; i < Bytes::cast(code.code()).length(); i++) {
    EXPECT_EQ(code.offsetToLineNum(i), 5);
  }
}

TEST_F(DoubleTest, DoubleTest) {
  RawObject o = runtime_.newFloat(3.14);
  ASSERT_TRUE(o.isFloat());
  RawFloat d = Float::cast(o);
  EXPECT_EQ(d.value(), 3.14);
}

TEST_F(ComplexTest, ComplexTest) {
  RawObject o = runtime_.newComplex(1.0, 2.0);
  ASSERT_TRUE(o.isComplex());
  RawComplex c = Complex::cast(o);
  EXPECT_EQ(c.real(), 1.0);
  EXPECT_EQ(c.imag(), 2.0);
}

TEST_F(IntTest, IntTest) {
  HandleScope scope(thread_);

  Object o1(&scope, runtime_.newInt(42));
  EXPECT_TRUE(isIntEqualsWord(*o1, 42));

  Object o2(&scope, runtime_.newInt(9223372036854775807L));
  EXPECT_TRUE(isIntEqualsWord(*o2, 9223372036854775807L));

  int stack_val = 123;
  Int o3(&scope, runtime_.newIntFromCPtr(&stack_val));
  EXPECT_EQ(*static_cast<int*>(o3.asCPtr()), 123);

  Object o4(&scope, runtime_.newInt(kMinWord));
  EXPECT_TRUE(isIntEqualsWord(*o4, kMinWord));

  uword digits[] = {kMaxUword, 0};
  Int o5(&scope, runtime_.newIntWithDigits(digits));
  EXPECT_TRUE(o5.isLargeInt());
  EXPECT_EQ(o5.bitLength(), kBitsPerWord);

  uword digits2[] = {kMaxUword, 1};
  Int o6(&scope, runtime_.newIntWithDigits(digits2));
  EXPECT_TRUE(o6.isLargeInt());
  EXPECT_EQ(o6.bitLength(), kBitsPerWord + 1);
}

TEST_F(IntTest, LargeIntValid) {
  HandleScope scope(thread_);

  LargeInt i(&scope, runtime_.heap()->createLargeInt(2));
  i.digitAtPut(0, -1234);
  i.digitAtPut(1, -1);
  // Redundant sign-extension
  EXPECT_FALSE(i.isValid());

  i.digitAtPut(1, -2);
  EXPECT_TRUE(i.isValid());

  i.digitAtPut(0, 1234);
  i.digitAtPut(1, 0);
  // Redundant zero-extension
  EXPECT_FALSE(i.isValid());

  i.digitAtPut(1, 1);
  EXPECT_TRUE(i.isValid());
}

TEST_F(IntTest, IsPositive) {
  HandleScope scope(thread_);

  Int zero(&scope, runtime_.newInt(0));
  EXPECT_FALSE(zero.isPositive());

  Int one(&scope, runtime_.newInt(1));
  EXPECT_TRUE(one.isPositive());

  Int neg_one(&scope, runtime_.newInt(-1));
  EXPECT_FALSE(neg_one.isPositive());

  Int max_small_int(&scope, runtime_.newInt(RawSmallInt::kMaxValue));
  EXPECT_TRUE(max_small_int.isPositive());

  Int min_small_int(&scope, runtime_.newInt(RawSmallInt::kMinValue));
  EXPECT_FALSE(min_small_int.isPositive());

  Int max_word(&scope, runtime_.newInt(kMaxWord));
  EXPECT_TRUE(max_word.isPositive());

  Int min_word(&scope, runtime_.newInt(kMinWord));
  EXPECT_FALSE(min_word.isPositive());
}

TEST_F(IntTest, IsNegative) {
  HandleScope scope(thread_);

  Int zero(&scope, runtime_.newInt(0));
  EXPECT_FALSE(zero.isNegative());

  Int one(&scope, runtime_.newInt(1));
  EXPECT_FALSE(one.isNegative());

  Int neg_one(&scope, runtime_.newInt(-1));
  EXPECT_TRUE(neg_one.isNegative());

  Int max_small_int(&scope, runtime_.newInt(RawSmallInt::kMaxValue));
  EXPECT_FALSE(max_small_int.isNegative());

  Int min_small_int(&scope, runtime_.newInt(RawSmallInt::kMinValue));
  EXPECT_TRUE(min_small_int.isNegative());

  Int max_word(&scope, runtime_.newInt(kMaxWord));
  EXPECT_FALSE(max_word.isNegative());

  Int min_word(&scope, runtime_.newInt(kMinWord));
  EXPECT_TRUE(min_word.isNegative());
}

TEST_F(IntTest, IsZero) {
  HandleScope scope(thread_);

  Int zero(&scope, runtime_.newInt(0));
  EXPECT_TRUE(zero.isZero());

  Int one(&scope, runtime_.newInt(1));
  EXPECT_FALSE(one.isZero());

  Int neg_one(&scope, runtime_.newInt(-1));
  EXPECT_FALSE(neg_one.isZero());

  Int max_small_int(&scope, runtime_.newInt(RawSmallInt::kMaxValue));
  EXPECT_FALSE(max_small_int.isZero());

  Int min_small_int(&scope, runtime_.newInt(RawSmallInt::kMinValue));
  EXPECT_FALSE(min_small_int.isZero());

  Int max_word(&scope, runtime_.newInt(kMaxWord));
  EXPECT_FALSE(max_word.isZero());

  Int min_word(&scope, runtime_.newInt(kMinWord));
  EXPECT_FALSE(min_word.isZero());
}

TEST_F(IntTest, Compare) {
  HandleScope scope(thread_);

  Int zero(&scope, runtime_.newInt(0));
  Int one(&scope, runtime_.newInt(1));
  Int neg_one(&scope, runtime_.newInt(-1));

  EXPECT_EQ(zero.compare(*zero), 0);
  EXPECT_GE(one.compare(*neg_one), 1);
  EXPECT_LE(neg_one.compare(*one), -1);

  Int min_small_int(&scope, runtime_.newInt(RawSmallInt::kMinValue));
  Int max_small_int(&scope, runtime_.newInt(RawSmallInt::kMaxValue));

  EXPECT_GE(max_small_int.compare(*min_small_int), 1);
  EXPECT_LE(min_small_int.compare(*max_small_int), -1);
  EXPECT_EQ(min_small_int.compare(*min_small_int), 0);
  EXPECT_EQ(max_small_int.compare(*max_small_int), 0);

  Int min_word(&scope, runtime_.newInt(kMinWord));
  Int max_word(&scope, runtime_.newInt(kMaxWord));

  EXPECT_GE(max_word.compare(*min_word), 1);
  EXPECT_LE(min_word.compare(*max_word), -1);
  EXPECT_EQ(min_word.compare(*min_word), 0);
  EXPECT_EQ(max_word.compare(*max_word), 0);

  EXPECT_GE(max_word.compare(*max_small_int), 1);
  EXPECT_LE(min_word.compare(*min_small_int), -1);
}

TEST_F(IntTest, LargeIntCompare) {
  HandleScope scope(thread_);
  const uword digits_great[] = {1, 1};
  Int great(&scope, newIntWithDigits(&runtime_, digits_great));
  const uword digits_small[] = {0, 0, kMaxUword};
  Int small(&scope, newIntWithDigits(&runtime_, digits_small));
  EXPECT_EQ(great.compare(*small), 1);
  EXPECT_EQ(small.compare(*great), -1);

  const uword digits_great2[] = {1, 1, 1};
  const uword digits_small2[] = {1, 1};
  great = newIntWithDigits(&runtime_, digits_great2);
  small = newIntWithDigits(&runtime_, digits_small2);
  EXPECT_EQ(great.compare(*small), 1);
  EXPECT_EQ(small.compare(*great), -1);

  const uword digits_great3[] = {kMaxUword - 1, 1};
  const uword digits_small3[] = {2, 1};
  great = newIntWithDigits(&runtime_, digits_great3);
  small = newIntWithDigits(&runtime_, digits_small3);
  EXPECT_EQ(great.compare(*small), 1);
  EXPECT_EQ(small.compare(*great), -1);

  const uword digits_great4[] = {kMaxUword - 1, kMaxUword - 1};
  const uword digits_small4[] = {2, kMaxUword - 1};
  great = newIntWithDigits(&runtime_, digits_great4);
  small = newIntWithDigits(&runtime_, digits_small4);
  EXPECT_EQ(great.compare(*small), 1);
  EXPECT_EQ(small.compare(*great), -1);
}

#define EXPECT_VALID(expr, expected_value)                                     \
  {                                                                            \
    auto const result = (expr);                                                \
    EXPECT_EQ(result.error, CastError::None);                                  \
    EXPECT_EQ(result.value, expected_value);                                   \
  }

TEST_F(IntTest, AsIntWithZeroReturnsZero) {
  HandleScope scope(thread_);
  Int zero(&scope, runtime_.newInt(0));
  EXPECT_VALID(zero.asInt<int>(), 0);
  EXPECT_VALID(zero.asInt<unsigned>(), 0U);
  EXPECT_VALID(zero.asInt<unsigned long>(), 0UL);
  EXPECT_VALID(zero.asInt<unsigned long long>(), 0ULL);
}

TEST_F(IntTest, AsIntReturnsInt) {
  HandleScope scope(thread_);
  Int num(&scope, runtime_.newInt(1234));
  EXPECT_VALID(num.asInt<int>(), 1234);
  EXPECT_VALID(num.asInt<long>(), 1234);
  EXPECT_VALID(num.asInt<unsigned>(), 1234U);
  EXPECT_VALID(num.asInt<unsigned long>(), 1234UL);
}

TEST_F(IntTest, AsIntReturnsOverflow) {
  HandleScope scope(thread_);
  Int num(&scope, runtime_.newInt(1234));
  EXPECT_EQ(num.asInt<byte>().error, CastError::Overflow);
  EXPECT_EQ(num.asInt<int8_t>().error, CastError::Overflow);
  Int word_max(&scope, runtime_.newInt(kMaxWord));
  EXPECT_EQ(word_max.asInt<int32_t>().error, CastError::Overflow);
  Int word_min(&scope, runtime_.newInt(kMinWord));
  EXPECT_EQ(word_min.asInt<int32_t>().error, CastError::Overflow);
}

TEST_F(IntTest, AsIntWithNegativeIntReturnsInt) {
  HandleScope scope(thread_);
  Int neg_num(&scope, runtime_.newInt(-4567));
  EXPECT_VALID(neg_num.asInt<int16_t>(), -4567);
  Int neg_one(&scope, runtime_.newInt(-1));
  EXPECT_VALID(neg_one.asInt<int>(), -1);
}

TEST_F(IntTest, AsIntReturnsUnderflow) {
  HandleScope scope(thread_);
  Int neg_num(&scope, runtime_.newInt(-4567));
  EXPECT_EQ(neg_num.asInt<unsigned>().error, CastError::Underflow);
  EXPECT_EQ(neg_num.asInt<int8_t>().error, CastError::Underflow);
  Int neg_one(&scope, runtime_.newInt(-1));
  EXPECT_EQ(neg_one.asInt<unsigned>().error, CastError::Underflow);
  Int word_min(&scope, runtime_.newInt(kMinWord));
  EXPECT_EQ(word_min.asInt<uword>().error, CastError::Underflow);
}

TEST_F(IntTest, AsIntWithMaxInt32ReturnsInt) {
  HandleScope scope(thread_);
  Int int32_max(&scope, runtime_.newInt(kMaxInt32));
  EXPECT_VALID(int32_max.asInt<int32_t>(), kMaxInt32);
  EXPECT_EQ(int32_max.asInt<int16_t>().error, CastError::Overflow);
}

TEST_F(IntTest, AsIntWithMaxUwordReturnsInt) {
  HandleScope scope(thread_);
  Int uword_max(&scope, runtime_.newIntFromUnsigned(kMaxUword));
  EXPECT_VALID(uword_max.asInt<uword>(), kMaxUword);
  EXPECT_EQ(uword_max.asInt<word>().error, CastError::Overflow);
}

TEST_F(IntTest, AsIntWithMaxWordReturnsInt) {
  HandleScope scope(thread_);
  Int word_max(&scope, runtime_.newInt(kMaxWord));
  EXPECT_VALID(word_max.asInt<word>(), kMaxWord);
  EXPECT_VALID(word_max.asInt<uword>(), uword{kMaxWord});
}

TEST_F(IntTest, AsIntWithMinWordReturnsInt) {
  HandleScope scope(thread_);
  Int word_min(&scope, runtime_.newInt(kMinWord));
  EXPECT_VALID(word_min.asInt<word>(), kMinWord);
}

TEST_F(IntTest, AsIntWithNegativeLargeIntReturnsUnderflow) {
  HandleScope scope(thread_);
  uword digits[] = {0, kMaxUword};
  Int negative(&scope, runtime_.newIntWithDigits(digits));
  EXPECT_EQ(negative.asInt<word>().error, CastError::Underflow);
  EXPECT_EQ(negative.asInt<uword>().error, CastError::Underflow);
}

TEST_F(IntTest, AsIntWithTrueReturnsOne) {
  HandleScope scope(thread_);
  Int value(&scope, Bool::trueObj());
  EXPECT_VALID(value.asInt<word>(), 1);
  EXPECT_VALID(value.asInt<uint8_t>(), 1);
}

TEST_F(IntTest, AsIntWithFalseReturnsZero) {
  HandleScope scope(thread_);
  Int value(&scope, Bool::falseObj());
  EXPECT_VALID(value.asInt<uword>(), 0);
  EXPECT_VALID(value.asInt<int32_t>(), 0);
}

#undef EXPECT_VALID

TEST_F(IntTest, SmallIntFromWordTruncatedWithSmallNegativeNumberReturnsSelf) {
  EXPECT_EQ(SmallInt::fromWord(-1), SmallInt::fromWordTruncated(-1));
}

TEST_F(ModulesTest, TestCreate) {
  HandleScope scope(thread_);
  Object name(&scope, runtime_.newStrFromCStr("mymodule"));
  Module module(&scope, runtime_.newModule(name));
  EXPECT_EQ(module.name(), *name);
  EXPECT_TRUE(module.dict().isDict());
}

TEST_F(MutableBytesTest, BecomeStrTurnsObjectIntoSmallStr) {
  HandleScope scope(thread_);
  Object test_0(&scope, runtime_.emptyMutableBytes());
  ASSERT_TRUE(test_0.isMutableBytes());
  Object as_str_0(&scope, MutableBytes::cast(*test_0).becomeStr());
  EXPECT_TRUE(test_0.isMutableBytes());
  EXPECT_TRUE(as_str_0.isSmallStr());
  EXPECT_TRUE(isStrEqualsCStr(*as_str_0, ""));

  Str str(&scope, runtime_.newStrFromCStr("abcdefghi"));

  Object test_1(&scope, runtime_.newMutableBytesUninitialized(1));
  ASSERT_TRUE(test_1.isMutableBytes());
  MutableBytes::cast(*test_1).replaceFromWithStr(0, *str, 1);
  Object as_str_1(&scope, MutableBytes::cast(*test_1).becomeStr());
  EXPECT_TRUE(test_1.isMutableBytes());
  EXPECT_TRUE(as_str_1.isSmallStr());
  EXPECT_TRUE(isStrEqualsCStr(*as_str_1, "a"));

  Object test_m(&scope,
                runtime_.newMutableBytesUninitialized(SmallStr::kMaxLength));
  ASSERT_TRUE(test_m.isMutableBytes());
  MutableBytes::cast(*test_m).replaceFromWithStr(0, *str, SmallStr::kMaxLength);
  Object as_str_m(&scope, MutableBytes::cast(*test_m).becomeStr());
  EXPECT_TRUE(test_m.isMutableBytes());
  EXPECT_TRUE(as_str_m.isSmallStr());
  EXPECT_TRUE(isStrEqualsCStr(*as_str_m, "abcdefg"));
}

TEST_F(MutableBytesTest, BecomeStrTurnsObjectIntoLargeStr) {
  HandleScope scope(thread_);
  Str str(&scope, runtime_.newStrFromCStr("hello world!"));

  Object test(&scope, runtime_.newMutableBytesUninitialized(str.charLength()));
  ASSERT_TRUE(test.isMutableBytes());
  MutableBytes::cast(*test).replaceFromWithStr(0, *str, str.charLength());
  MutableBytes::cast(*test).becomeStr();
  EXPECT_TRUE(test.isLargeStr());
  EXPECT_TRUE(isStrEqualsCStr(*test, "hello world!"));
}

TEST_F(SliceTest, AdjustIndices) {
  // Test: 0:10:1 on len: 10
  word length = 10;
  word start = 0;
  word stop = 10;
  word step = 1;
  word new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 10);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 10);

  // Test: 2:10:1 on len: 10
  start = 2;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 8);
  ASSERT_EQ(start, 2);
  ASSERT_EQ(stop, 10);

  // Test: -4:10:1 on len: 10
  start = -4;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 4);
  ASSERT_EQ(start, 6);
  ASSERT_EQ(stop, 10);

  // Test: 0:2:1 on len: 10
  start = 0;
  stop = 2;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 2);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 2);

  // Test: 0:-2:1 on len: 10
  start = 0;
  stop = -2;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 8);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 8);

  // Test: 0:10:2 on len: 10
  start = 0;
  stop = 10;
  step = 2;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 5);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 10);

  // Test: 0:10:-2 on len: 10
  start = 0;
  stop = 10;
  step = -2;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 0);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 9);
}

TEST_F(SliceTest, AdjustIndicesOutOfBounds) {
  // Test: 10:5:1 on len: 5
  word length = 5;
  word start = 10;
  word stop = 5;
  word step = 1;
  word new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 0);
  ASSERT_EQ(start, 5);
  ASSERT_EQ(stop, 5);

  // Test: -10:5:1 on len: 5
  start = -10;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 5);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 5);

  // Test: 0:10:1 on len: 5
  start = 0;
  stop = 10;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 5);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 5);

  // Test: 0:-10:1 on len: 5
  stop = -10;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 0);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 0);

  // Test: 0:5:10 on len: 5
  stop = 5;
  step = 10;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 1);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 5);

  // Test: 0:5:-10 on len: 5
  step = -10;
  new_length = Slice::adjustIndices(length, &start, &stop, step);
  ASSERT_EQ(new_length, 0);
  ASSERT_EQ(start, 0);
  ASSERT_EQ(stop, 4);
}

TEST_F(SliceTest, LengthWithNegativeStepAndStopLessThanStartReturnsLength) {
  EXPECT_EQ(Slice::length(5, 2, -1), 3);
}

TEST_F(SliceTest, LengthWithNegativeStepAndStartLessThanStopReturnsZero) {
  EXPECT_EQ(Slice::length(2, 5, -1), 0);
}

TEST_F(SliceTest, LengthWithNegativeStepAndStartEqualsStopReturnsZero) {
  EXPECT_EQ(Slice::length(2, 2, -1), 0);
}

TEST_F(SliceTest, LengthWithPositiveStepAndStartLessThanStopReturnsLength) {
  EXPECT_EQ(Slice::length(2, 5, 1), 3);
}

TEST_F(SliceTest, LengthWithPositiveStepAndStopLessThanStartReturnsZero) {
  EXPECT_EQ(Slice::length(5, 2, 1), 0);
}

TEST_F(SliceTest, LengthWithPositiveStepAndStartEqualsStopReturnsZero) {
  EXPECT_EQ(Slice::length(2, 2, 1), 0);
}

TEST_F(LargeStrTest, CopyTo) {
  RawObject obj = runtime_.newStrFromCStr("hello world!");
  ASSERT_TRUE(obj.isLargeStr());
  RawStr str = Str::cast(obj);

  byte array[5];
  memset(array, 'a', ARRAYSIZE(array));
  str.copyTo(array, 0);
  EXPECT_EQ(array[0], 'a');
  EXPECT_EQ(array[1], 'a');
  EXPECT_EQ(array[2], 'a');
  EXPECT_EQ(array[3], 'a');
  EXPECT_EQ(array[4], 'a');

  memset(array, 'b', ARRAYSIZE(array));
  str.copyTo(array, 1);
  EXPECT_EQ(array[0], 'h');
  EXPECT_EQ(array[1], 'b');
  EXPECT_EQ(array[2], 'b');
  EXPECT_EQ(array[3], 'b');
  EXPECT_EQ(array[4], 'b');

  memset(array, 'c', ARRAYSIZE(array));
  str.copyTo(array, 5);
  EXPECT_EQ(array[0], 'h');
  EXPECT_EQ(array[1], 'e');
  EXPECT_EQ(array[2], 'l');
  EXPECT_EQ(array[3], 'l');
  EXPECT_EQ(array[4], 'o');
}

TEST_F(StringTest, CompareSmallStrCStrASCII) {
  HandleScope scope(thread_);

  Str small_ascii(&scope, runtime_.newStrFromCStr("sm"));
  ASSERT_TRUE(small_ascii.isSmallStr());

  // Equal
  EXPECT_EQ(small_ascii.compareCStr("sm"), 0);

  // Less
  EXPECT_EQ(small_ascii.compareCStr("sma"), -1);
  EXPECT_EQ(small_ascii.compareCStr("sn"), -1);

  // Greater
  EXPECT_EQ(small_ascii.compareCStr("s"), 1);
  EXPECT_EQ(small_ascii.compareCStr("sl"), 1);
}

TEST_F(StringTest, CompareSmallStrWithNulCStrASCII) {
  HandleScope scope(thread_);

  const byte data[] = {'s', '\0', 'm'};
  Str small_ascii(&scope, runtime_.newStrWithAll(data));
  ASSERT_TRUE(small_ascii.isSmallStr());

  // Less
  EXPECT_EQ(small_ascii.compareCStr("t"), -1);

  // Greater
  EXPECT_EQ(small_ascii.compareCStr("s"), 1);
  EXPECT_EQ(small_ascii.compareCStr("a\0m"), 1);
}

TEST_F(StringTest, CompareLargeStrWithNulCStrASCII) {
  HandleScope scope(thread_);

  const byte data[] = {'l', 'a', 'r', 'g', 'e', '\0', 's', 't'};
  Str large_ascii(&scope, runtime_.newStrWithAll(data));
  ASSERT_TRUE(large_ascii.isLargeStr());

  // Less
  EXPECT_EQ(large_ascii.compareCStr("largz"), -1);

  // Greater
  EXPECT_EQ(large_ascii.compareCStr("large"), 1);
  EXPECT_EQ(large_ascii.compareCStr("larga\0st"), 1);
}

TEST_F(StringTest, CompareLargeStrCStrASCII) {
  HandleScope scope(thread_);

  Str large_ascii(&scope, runtime_.newStrFromCStr("large string"));
  ASSERT_TRUE(large_ascii.isLargeStr());

  // Equal
  EXPECT_EQ(large_ascii.compareCStr("large string"), 0);

  // Less
  EXPECT_EQ(large_ascii.compareCStr("large strings"), -1);
  EXPECT_EQ(large_ascii.compareCStr("large tbigger"), -1);

  // Greater
  EXPECT_EQ(large_ascii.compareCStr("large strin"), 1);
  EXPECT_EQ(large_ascii.compareCStr("large smaller"), 1);
}

TEST_F(StringTest, CompareSmallStrCStrUTF8) {
  HandleScope scope(thread_);

  Str small_utf8(&scope, runtime_.newStrFromCStr("\xC3\x87"));
  ASSERT_TRUE(small_utf8.isSmallStr());

  // Equal
  EXPECT_EQ(small_utf8.compareCStr("\xC3\x87"), 0);

  // Less
  EXPECT_EQ(small_utf8.compareCStr("\xC3\x87s"), -1);
  EXPECT_EQ(small_utf8.compareCStr("\xC3\x88"), -1);
  EXPECT_EQ(small_utf8.compareCStr("\xC3\xA7"), -1);

  // Greater
  EXPECT_EQ(small_utf8.compareCStr(""), 1);
  EXPECT_EQ(small_utf8.compareCStr("\xC3\x86"), 1);
  EXPECT_EQ(small_utf8.compareCStr("\xC3\x67"), 1);
}

TEST_F(StringTest, CompareLargeStrCStrUTF8) {
  HandleScope scope(thread_);

  Str large_utf8(&scope, runtime_.newStrFromCStr("\xC3\x87 large"));
  ASSERT_TRUE(large_utf8.isLargeStr());

  // Equal
  EXPECT_EQ(large_utf8.compareCStr("\xC3\x87 large"), 0);

  // Less
  EXPECT_EQ(large_utf8.compareCStr("\xC3\x87 larges"), -1);
  EXPECT_EQ(large_utf8.compareCStr("\xC3\x88 large"), -1);
  EXPECT_EQ(large_utf8.compareCStr("\xC3\xA7 large"), -1);

  // Greater
  EXPECT_EQ(large_utf8.compareCStr("\xC3\x87"), 1);
  EXPECT_EQ(large_utf8.compareCStr("\xC3\x86 large"), 1);
  EXPECT_EQ(large_utf8.compareCStr("g large"), 1);
}

TEST_F(StringTest, CompareSmallStrCStrLatin1) {
  HandleScope scope(thread_);

  Str small_latin1(&scope, runtime_.newStrFromCStr("\xDC"));
  ASSERT_TRUE(small_latin1.isSmallStr());

  // Equal
  EXPECT_EQ(small_latin1.compareCStr("\xDC"), 0);

  // Less
  EXPECT_EQ(small_latin1.compareCStr("\xDCs"), -1);
  EXPECT_EQ(small_latin1.compareCStr("\xDD"), -1);
  EXPECT_EQ(small_latin1.compareCStr("\xEC"), -1);

  // Greater
  EXPECT_EQ(small_latin1.compareCStr(""), 1);
  EXPECT_EQ(small_latin1.compareCStr("\xDB"), 1);
  EXPECT_EQ(small_latin1.compareCStr("\xAC"), 1);
}

TEST_F(StringTest, CompareLargeStrCStrLatin1) {
  HandleScope scope(thread_);

  Str large_latin1(&scope, runtime_.newStrFromCStr("\xDClarge str"));
  ASSERT_TRUE(large_latin1.isLargeStr());

  // Equal
  EXPECT_EQ(large_latin1.compareCStr("\xDClarge str"), 0);

  // Less
  EXPECT_EQ(large_latin1.compareCStr("\xDClarge strs"), -1);
  EXPECT_EQ(large_latin1.compareCStr("\xDDlarge str"), -1);
  EXPECT_EQ(large_latin1.compareCStr("\xEClarge str"), -1);

  // Greater
  EXPECT_EQ(large_latin1.compareCStr("\xDC"), 1);
  EXPECT_EQ(large_latin1.compareCStr("\xDBlarge str"), 1);
  EXPECT_EQ(large_latin1.compareCStr("\xBClarge str"), 1);
}

TEST_F(StringTest, CopyToStartAtWithLargeStrCopiesBytes) {
  HandleScope scope(thread_);
  Str str(&scope, runtime_.newStrFromCStr("Hello world!"));

  byte actual0[5];
  str.copyToStartAt(actual0, 5, 3);
  EXPECT_EQ(std::memcmp(actual0, "lo", 2), 0);

  byte actual1[3];
  str.copyToStartAt(actual1, 3, 4);
  EXPECT_EQ(std::memcmp(actual1, "o w", 3), 0);

  // zero-sized copies should do nothing.
  str.copyToStartAt(nullptr, 0, 0);
  str.copyToStartAt(nullptr, 0, 12);
}

TEST_F(StringTest, CopyToStartAtWithSmallStrCopiesBytes) {
  HandleScope scope(thread_);
  Str str(&scope, SmallStr::fromCStr("bar"));

  byte actual0[3];
  str.copyToStartAt(actual0, 3, 0);
  EXPECT_EQ(std::memcmp(actual0, "bar", 3), 0);

  byte actual1[2];
  str.copyToStartAt(actual1, 2, 1);
  EXPECT_EQ(std::memcmp(actual1, "ar", 2), 0);

  // zero-sized copies should do nothing.
  str.copyToStartAt(nullptr, 0, 0);
  str.copyToStartAt(nullptr, 0, 3);
}

TEST(SmallStrTest, Tests) {
  RawObject obj0 = SmallStr::fromCStr("AB");
  ASSERT_TRUE(obj0.isSmallStr());
  auto str0 = Str::cast(obj0);
  EXPECT_EQ(str0.charLength(), 2);
  EXPECT_EQ(str0.charAt(0), 'A');
  EXPECT_EQ(str0.charAt(1), 'B');
  byte array[3]{0, 0, 0};
  str0.copyTo(array, 2);
  EXPECT_EQ(array[0], 'A');
  EXPECT_EQ(array[1], 'B');
  EXPECT_EQ(array[2], 0);
}

TEST(SmallStrTest, CodePointLengthWithAsciiReturnsLength) {
  RawObject len0 = SmallStr::fromCStr("");
  ASSERT_TRUE(len0.isSmallStr());
  EXPECT_EQ(Str::cast(len0).charLength(), 0);
  EXPECT_EQ(Str::cast(len0).codePointLength(), 0);

  RawObject len1 = SmallStr::fromCStr("1");
  ASSERT_TRUE(len1.isSmallStr());
  EXPECT_EQ(Str::cast(len1).charLength(), 1);
  EXPECT_EQ(Str::cast(len1).codePointLength(), 1);

  RawObject len2 = SmallStr::fromCStr("12");
  ASSERT_TRUE(len2.isSmallStr());
  EXPECT_EQ(Str::cast(len2).charLength(), 2);
  EXPECT_EQ(Str::cast(len2).codePointLength(), 2);

  RawObject len3 = SmallStr::fromCStr("123");
  ASSERT_TRUE(len3.isSmallStr());
  EXPECT_EQ(Str::cast(len3).charLength(), 3);
  EXPECT_EQ(Str::cast(len3).codePointLength(), 3);
}

TEST(SmallStrTest, CodePointLengthWithOneCodePoint) {
  RawObject len1 = SmallStr::fromCStr("\x24");
  ASSERT_TRUE(len1.isSmallStr());
  EXPECT_EQ(Str::cast(len1).charLength(), 1);
  EXPECT_EQ(Str::cast(len1).codePointLength(), 1);

  RawObject len2 = SmallStr::fromCStr("\xC2\xA2");
  ASSERT_TRUE(len2.isSmallStr());
  EXPECT_EQ(Str::cast(len2).charLength(), 2);
  EXPECT_EQ(Str::cast(len2).codePointLength(), 1);

  RawObject len3 = SmallStr::fromCStr("\xE0\xA4\xB9");
  ASSERT_TRUE(len3.isSmallStr());
  EXPECT_EQ(Str::cast(len3).charLength(), 3);
  EXPECT_EQ(Str::cast(len3).codePointLength(), 1);

  RawObject len4 = SmallStr::fromCStr("\xF0\x90\x8D\x88");
  ASSERT_TRUE(len4.isSmallStr());
  EXPECT_EQ(Str::cast(len4).charLength(), 4);
  EXPECT_EQ(Str::cast(len4).codePointLength(), 1);
}

TEST(SmallStrTest, CodePointLengthWithTwoCodePoints) {
  RawObject len1 = SmallStr::fromCStr("\x24\x65");
  ASSERT_TRUE(len1.isSmallStr());
  EXPECT_EQ(Str::cast(len1).charLength(), 2);
  EXPECT_EQ(Str::cast(len1).codePointLength(), 2);

  RawObject len2 = SmallStr::fromCStr("\xC2\xA2\xC2\xA3");
  ASSERT_TRUE(len2.isSmallStr());
  EXPECT_EQ(Str::cast(len2).charLength(), 4);
  EXPECT_EQ(Str::cast(len2).codePointLength(), 2);

  RawObject len3 = SmallStr::fromCStr("\xE0\xA4\xB9\xC2\xA3");
  ASSERT_TRUE(len3.isSmallStr());
  EXPECT_EQ(Str::cast(len3).charLength(), 5);
  EXPECT_EQ(Str::cast(len3).codePointLength(), 2);

  RawObject len4 = SmallStr::fromCStr("\xF0\x90\x8D\x88\xC2\xA3");
  ASSERT_TRUE(len4.isSmallStr());
  EXPECT_EQ(Str::cast(len4).charLength(), 6);
  EXPECT_EQ(Str::cast(len4).codePointLength(), 2);
}

TEST(SmallStrTest, CodePointLengthWithThreeCodePoints) {
  RawObject len1 = SmallStr::fromCStr("\x24\x65\x66");
  ASSERT_TRUE(len1.isSmallStr());
  EXPECT_EQ(Str::cast(len1).charLength(), 3);
  EXPECT_EQ(Str::cast(len1).codePointLength(), 3);

  RawObject len2 = SmallStr::fromCStr("\xC2\xA2\xC2\xA3\xC2\xA4");
  ASSERT_TRUE(len2.isSmallStr());
  EXPECT_EQ(Str::cast(len2).charLength(), 6);
  EXPECT_EQ(Str::cast(len2).codePointLength(), 3);

  RawObject len3 = SmallStr::fromCStr("\xE0\xA4\xB9\xC2\xA3\xC2\xA4");
  ASSERT_TRUE(len3.isSmallStr());
  EXPECT_EQ(Str::cast(len3).charLength(), 7);
  EXPECT_EQ(Str::cast(len3).codePointLength(), 3);

  RawObject len4 = SmallStr::fromCStr("\xF0\x90\x8D\x88\x65\xC2\xA3");
  ASSERT_TRUE(len4.isSmallStr());
  EXPECT_EQ(Str::cast(len4).charLength(), 7);
  EXPECT_EQ(Str::cast(len4).codePointLength(), 3);
}

TEST(SmallStrTest, FromCodePointOneByte) {
  RawObject obj = SmallStr::fromCodePoint(0x24);
  ASSERT_TRUE(obj.isSmallStr());
  RawStr str = Str::cast(obj);
  ASSERT_EQ(str.charLength(), 1);
  EXPECT_EQ(str.charAt(0), 0x24);
}

TEST(SmallStrTest, FromCodePointTwoByte) {
  RawObject obj = SmallStr::fromCodePoint(0xA2);
  ASSERT_TRUE(obj.isSmallStr());
  RawStr str = Str::cast(obj);
  ASSERT_EQ(str.charLength(), 2);
  EXPECT_EQ(str.charAt(0), 0xC2);
  EXPECT_EQ(str.charAt(1), 0xA2);
}

TEST(SmallStrTest, FromCodePointThreeByte) {
  RawObject obj1 = SmallStr::fromCodePoint(0x0939);
  ASSERT_TRUE(obj1.isSmallStr());
  RawStr str1 = Str::cast(obj1);
  ASSERT_EQ(str1.charLength(), 3);
  EXPECT_EQ(str1.charAt(0), 0xE0);
  EXPECT_EQ(str1.charAt(1), 0xA4);
  EXPECT_EQ(str1.charAt(2), 0xB9);

  RawObject obj2 = SmallStr::fromCodePoint(0x20AC);
  ASSERT_TRUE(obj2.isSmallStr());
  RawStr str2 = Str::cast(obj2);
  ASSERT_EQ(str2.charLength(), 3);
  EXPECT_EQ(str2.charAt(0), 0xE2);
  EXPECT_EQ(str2.charAt(1), 0x82);
  EXPECT_EQ(str2.charAt(2), 0xAC);
}

TEST(SmallStrTest, FromCodePointFourByte) {
  RawObject obj = SmallStr::fromCodePoint(0x10348);
  ASSERT_TRUE(obj.isSmallStr());
  RawStr str = Str::cast(obj);
  ASSERT_EQ(str.charLength(), 4);
  EXPECT_EQ(str.charAt(0), 0xF0);
  EXPECT_EQ(str.charAt(1), 0x90);
  EXPECT_EQ(str.charAt(2), 0x8D);
  EXPECT_EQ(str.charAt(3), 0x88);
}

TEST_F(StrTest, OffsetByCodePoints) {
  HandleScope scope(thread_);

  Str empty(&scope, Str::empty());
  EXPECT_EQ(empty.charLength(), 0);
  EXPECT_EQ(empty.codePointLength(), 0);
  EXPECT_EQ(empty.offsetByCodePoints(0, 1), 0);
  EXPECT_EQ(empty.offsetByCodePoints(2, 0), 0);
  EXPECT_EQ(empty.offsetByCodePoints(2, 1), 0);

  Str ascii(&scope, runtime_.newStrFromCStr("abcd"));
  EXPECT_EQ(ascii.charLength(), 4);
  EXPECT_EQ(ascii.codePointLength(), 4);

  // for ASCII, each code point is one byte wide
  EXPECT_EQ(ascii.offsetByCodePoints(0, 0), 0);
  EXPECT_EQ(ascii.offsetByCodePoints(0, 3), 3);
  EXPECT_EQ(ascii.offsetByCodePoints(1, 0), 1);
  EXPECT_EQ(ascii.offsetByCodePoints(2, 0), 2);
  EXPECT_EQ(ascii.offsetByCodePoints(2, 1), 3);
  EXPECT_EQ(ascii.offsetByCodePoints(3, 0), 3);

  // return the length once we reach the end of the string
  EXPECT_EQ(ascii.offsetByCodePoints(0, 4), 4);
  EXPECT_EQ(ascii.offsetByCodePoints(0, 5), 4);
  EXPECT_EQ(ascii.offsetByCodePoints(1, 3), 4);
  EXPECT_EQ(ascii.offsetByCodePoints(1, 4), 4);
  EXPECT_EQ(ascii.offsetByCodePoints(2, 2), 4);
  EXPECT_EQ(ascii.offsetByCodePoints(2, 3), 4);
  EXPECT_EQ(ascii.offsetByCodePoints(3, 1), 4);
  EXPECT_EQ(ascii.offsetByCodePoints(3, 2), 4);
  EXPECT_EQ(ascii.offsetByCodePoints(4, 0), 4);
  EXPECT_EQ(ascii.offsetByCodePoints(6, 0), 4);

  Str unicode(&scope,
              runtime_.newStrFromCStr("\xd7\x90pq\xd7\x91\xd7\x92-\xd7\x93"));
  EXPECT_EQ(unicode.charLength(), 11);
  EXPECT_EQ(unicode.codePointLength(), 7);

  // for Unicode, code points may be more than one byte wide
  EXPECT_EQ(unicode.offsetByCodePoints(0, 0), 0);
  EXPECT_EQ(unicode.offsetByCodePoints(0, 1), 2);
  EXPECT_EQ(unicode.offsetByCodePoints(0, 2), 3);
  EXPECT_EQ(unicode.offsetByCodePoints(0, 3), 4);
  EXPECT_EQ(unicode.offsetByCodePoints(0, 4), 6);
  EXPECT_EQ(unicode.offsetByCodePoints(0, 5), 8);
  EXPECT_EQ(unicode.offsetByCodePoints(0, 6), 9);
  EXPECT_EQ(unicode.offsetByCodePoints(2, 0), 2);
  EXPECT_EQ(unicode.offsetByCodePoints(2, 1), 3);
  EXPECT_EQ(unicode.offsetByCodePoints(2, 2), 4);
  EXPECT_EQ(unicode.offsetByCodePoints(2, 3), 6);
  EXPECT_EQ(unicode.offsetByCodePoints(2, 4), 8);
  EXPECT_EQ(unicode.offsetByCodePoints(2, 5), 9);
  EXPECT_EQ(unicode.offsetByCodePoints(2, 6), 11);
  EXPECT_EQ(unicode.offsetByCodePoints(4, 0), 4);
  EXPECT_EQ(unicode.offsetByCodePoints(4, 1), 6);
  EXPECT_EQ(unicode.offsetByCodePoints(6, 0), 6);

  // return the length once we reach the end of the string
  EXPECT_EQ(unicode.offsetByCodePoints(0, 7), 11);
  EXPECT_EQ(unicode.offsetByCodePoints(0, 9), 11);
  EXPECT_EQ(unicode.offsetByCodePoints(2, 7), 11);
  EXPECT_EQ(unicode.offsetByCodePoints(3, 6), 11);
  EXPECT_EQ(unicode.offsetByCodePoints(4, 5), 11);
  EXPECT_EQ(unicode.offsetByCodePoints(8, 3), 11);
  EXPECT_EQ(unicode.offsetByCodePoints(12, 0), 11);
}

TEST_F(LargeStrTest, CodePointLengthAscii) {
  HandleScope scope(thread_);

  const char* code_units = "01234567012345670";

  Str str(&scope, runtime_.newStrFromCStr(code_units));
  EXPECT_TRUE(str.isLargeStr());
  EXPECT_EQ(str.charLength(), std::strlen(code_units));
  EXPECT_EQ(str.codePointLength(), 17);
}

TEST_F(LargeStrTest, CodePointLength) {
  HandleScope scope(thread_);

  const char* code_units =
      "\xd7\x99\xd7\xa9 \xd7\x9c\xd7\x99 \xd7\x94\xd7\xa8\xd7\x91\xd7\x94 "
      "\xd7\x90\xd7\x95\xd7\xaa\xd7\x99\xd7\x95\xd7\xaa "
      "\xd7\xa2\xd7\x9b\xd7\xa9\xd7\x99\xd7\x95";

  Str str(&scope, runtime_.newStrFromCStr(code_units));
  EXPECT_TRUE(str.isLargeStr());
  EXPECT_EQ(str.charLength(), std::strlen(code_units));
  EXPECT_EQ(str.codePointLength(), 23);
}

TEST_F(StringTest, ReverseOffsetByCodePointsEmptyString) {
  HandleScope scope(thread_);
  Str empty(&scope, Str::empty());

  word i = empty.charLength();
  ASSERT_EQ(0, i);

  EXPECT_EQ(-1, empty.offsetByCodePoints(i, -1));
}

TEST_F(StringTest, ReverseOffsetByCodePointsStringLength1) {
  HandleScope scope(thread_);

  Str str1(&scope, runtime_.newStrFromCStr("1"));
  word len = str1.charLength();
  ASSERT_EQ(1, len);

  EXPECT_EQ(1, str1.offsetByCodePoints(len, 0));
  EXPECT_EQ(0, str1.offsetByCodePoints(len, -1));
  EXPECT_EQ(-1, str1.offsetByCodePoints(len, -2));
}

TEST_F(StringTest, ReverseOffsetByCodePointsStringLength3) {
  HandleScope scope(thread_);

  Str str3(&scope, runtime_.newStrFromCStr("123"));
  word len = str3.charLength();
  ASSERT_EQ(3, len);

  EXPECT_EQ(2, str3.offsetByCodePoints(len, -1));
  EXPECT_EQ(1, str3.offsetByCodePoints(len, -2));
  EXPECT_EQ(0, str3.offsetByCodePoints(len, -3));
  EXPECT_EQ(-1, str3.offsetByCodePoints(len, -4));

  EXPECT_EQ(1, str3.offsetByCodePoints(len - 1, -1));
  EXPECT_EQ(0, str3.offsetByCodePoints(len - 1, -2));
  EXPECT_EQ(-1, str3.offsetByCodePoints(len - 1, -3));
  EXPECT_EQ(-1, str3.offsetByCodePoints(len - 1, -4));

  EXPECT_EQ(0, str3.offsetByCodePoints(len - 2, -1));
  EXPECT_EQ(-1, str3.offsetByCodePoints(len - 2, -2));
  EXPECT_EQ(-1, str3.offsetByCodePoints(len - 2, -3));
  EXPECT_EQ(-1, str3.offsetByCodePoints(len - 2, -4));
}

TEST_F(StringTest, ReverseOffsetByCodePointsUnicodeStringLength5) {
  HandleScope scope(thread_);

  Str str5(&scope, runtime_.newStrFromCStr("\x41\xD7\x91\xD7\x92"));
  word len = str5.charLength();
  ASSERT_EQ(5, len);

  EXPECT_EQ(3, str5.offsetByCodePoints(len, -1));
  EXPECT_EQ(1, str5.offsetByCodePoints(len, -2));
  EXPECT_EQ(0, str5.offsetByCodePoints(len, -3));
  EXPECT_EQ(-1, str5.offsetByCodePoints(len, -4));

  EXPECT_EQ(1, str5.offsetByCodePoints(len - 2, -1));
  EXPECT_EQ(0, str5.offsetByCodePoints(len - 2, -2));
  EXPECT_EQ(-1, str5.offsetByCodePoints(len - 2, -3));
  EXPECT_EQ(-1, str5.offsetByCodePoints(len - 2, -4));

  EXPECT_EQ(0, str5.offsetByCodePoints(len - 4, -1));
  EXPECT_EQ(-1, str5.offsetByCodePoints(len - 4, -2));
  EXPECT_EQ(-1, str5.offsetByCodePoints(len - 4, -3));
  EXPECT_EQ(-1, str5.offsetByCodePoints(len - 4, -4));
}

TEST_F(StringTest, ToCString) {
  HandleScope scope(thread_);

  Str empty(&scope, Str::empty());
  char* c_empty = empty.toCStr();
  ASSERT_NE(c_empty, nullptr);
  EXPECT_STREQ(c_empty, "");
  std::free(c_empty);

  Str length1(&scope, runtime_.newStrFromCStr("a"));
  char* c_length1 = length1.toCStr();
  ASSERT_NE(c_length1, nullptr);
  EXPECT_STREQ(c_length1, "a");
  std::free(c_length1);

  Str length2(&scope, runtime_.newStrFromCStr("ab"));
  char* c_length2 = length2.toCStr();
  ASSERT_NE(c_length2, nullptr);
  EXPECT_STREQ(c_length2, "ab");
  std::free(c_length2);

  Str length10(&scope, runtime_.newStrFromCStr("1234567890"));
  char* c_length10 = length10.toCStr();
  ASSERT_NE(c_length10, nullptr);
  EXPECT_STREQ(c_length10, "1234567890");
  std::free(c_length10);

  Str nulchar(&scope, runtime_.newStrFromCStr("wx\0yz"));
  char* c_nulchar = nulchar.toCStr();
  ASSERT_NE(c_nulchar, nullptr);
  EXPECT_STREQ(c_nulchar, "wx");
  std::free(c_nulchar);
}

TEST_F(StringTest, CompareSmallStr) {
  HandleScope scope(thread_);

  Str small(&scope, runtime_.newStrFromCStr("foo"));
  EXPECT_TRUE(small.isSmallStr());

  EXPECT_TRUE(small.equalsCStr("foo"));
  // This apparently stupid test is in response to a bug where we assumed
  // that the c-string passed to SmallStr::equalsCStr would always
  // be small itself.
  EXPECT_FALSE(small.equalsCStr("123456789"));
}

TEST_F(StringTest, CompareWithUnicode) {
  HandleScope scope(thread_);
  Str small(&scope, runtime_.newStrFromCStr(u8"hello\u2028"));
  EXPECT_TRUE(small.equalsCStr("hello\u2028"));
}

TEST_F(ValueCellTest, SetPlaceholderRendersIsPlaceholderToReturnTrue) {
  HandleScope scope(thread_);
  ValueCell value_cell(&scope, runtime_.newValueCell());
  ASSERT_FALSE(value_cell.isPlaceholder());
  value_cell.makePlaceholder();
  EXPECT_TRUE(value_cell.isPlaceholder());
}

TEST_F(WeakRefTest, EnqueueAndDequeue) {
  HandleScope scope(thread_);
  RawObject list = NoneType::object();
  for (int i = 0; i < 3; i++) {
    Object obj(&scope, SmallInt::fromWord(i));
    Object none(&scope, NoneType::object());
    WeakRef weak(&scope, runtime_.newWeakRef(thread_, obj, none));
    WeakRef::enqueue(*weak, &list);
  }
  WeakRef weak(&scope, WeakRef::dequeue(&list));
  EXPECT_TRUE(isIntEqualsWord(weak.referent(), 0));

  weak = WeakRef::dequeue(&list);
  EXPECT_TRUE(isIntEqualsWord(weak.referent(), 1));

  weak = WeakRef::dequeue(&list);
  EXPECT_TRUE(isIntEqualsWord(weak.referent(), 2));

  EXPECT_EQ(list, NoneType::object());
}

TEST_F(WeakRefTest, SpliceQueue) {
  HandleScope scope(thread_);
  RawObject list1 = NoneType::object();
  RawObject list2 = NoneType::object();
  EXPECT_EQ(WeakRef::spliceQueue(list1, list2), NoneType::object());

  Object none(&scope, NoneType::object());
  RawObject list3 = runtime_.newWeakRef(thread_, none, none);
  WeakRef::cast(list3).setLink(list3);
  EXPECT_EQ(WeakRef::spliceQueue(list1, list3), list3);
  EXPECT_EQ(WeakRef::spliceQueue(list3, list2), list3);

  for (int i = 0; i < 2; i++) {
    Object obj1(&scope, SmallInt::fromWord(i));
    WeakRef weak1(&scope, runtime_.newWeakRef(thread_, obj1, none));
    weak1.setReferent(SmallInt::fromWord(i));
    WeakRef::enqueue(*weak1, &list1);

    Object obj2(&scope, SmallInt::fromWord(i + 2));
    WeakRef weak2(&scope, runtime_.newWeakRef(thread_, obj2, none));
    WeakRef::enqueue(*weak2, &list2);
  }
  RawObject list = WeakRef::spliceQueue(list1, list2);
  WeakRef weak(&scope, WeakRef::dequeue(&list));
  EXPECT_TRUE(isIntEqualsWord(weak.referent(), 0));

  weak = WeakRef::dequeue(&list);
  EXPECT_TRUE(isIntEqualsWord(weak.referent(), 1));

  weak = WeakRef::dequeue(&list);
  EXPECT_TRUE(isIntEqualsWord(weak.referent(), 2));

  weak = WeakRef::dequeue(&list);
  EXPECT_TRUE(isIntEqualsWord(weak.referent(), 3));

  EXPECT_EQ(list, NoneType::object());
}

TEST_F(ListTest, ReplaceFromWithReplacesElementsStartingAtZero) {
  HandleScope scope(thread_);
  List dst(&scope, runtime_.newList());
  Tuple dst_tuple(&scope, runtime_.newMutableTuple(5));
  dst.setItems(*dst_tuple);
  dst.setNumItems(5);
  List src(&scope, listFromRange(0, 5));
  dst.replaceFromWith(0, *src, 2);
  EXPECT_PYLIST_EQ(dst, {0, 1, Value::none(), Value::none(), Value::none()});
}

TEST_F(ListTest, ReplaceFromWithReplacesElementsStartingInMiddle) {
  HandleScope scope(thread_);
  List dst(&scope, runtime_.newList());
  Tuple dst_tuple(&scope, runtime_.newMutableTuple(5));
  dst.setItems(*dst_tuple);
  dst.setNumItems(5);
  List src(&scope, listFromRange(0, 5));
  dst.replaceFromWith(1, *src, 2);
  EXPECT_PYLIST_EQ(dst, {Value::none(), 0, 1, Value::none(), Value::none()});
}

TEST_F(ListTest, ReplaceFromWithCopiesZeroElements) {
  HandleScope scope(thread_);
  List dst(&scope, runtime_.newList());
  Tuple dst_tuple(&scope, runtime_.newMutableTuple(5));
  dst.setItems(*dst_tuple);
  dst.setNumItems(5);
  List src(&scope, listFromRange(0, 5));
  dst.replaceFromWith(0, *src, 0);
  EXPECT_PYLIST_EQ(dst, {Value::none(), Value::none(), Value::none(),
                         Value::none(), Value::none()});
}

TEST_F(ListTest, ReplaceFromWithCopiesEveryElementFromSrc) {
  HandleScope scope(thread_);
  List dst(&scope, runtime_.newList());
  Tuple dst_tuple(&scope, runtime_.newMutableTuple(5));
  dst.setItems(*dst_tuple);
  dst.setNumItems(5);
  List src(&scope, listFromRange(0, 5));
  dst.replaceFromWith(0, *src, 5);
  EXPECT_PYLIST_EQ(dst, {0, 1, 2, 3, 4});
}

TEST_F(ListTest, ReplaceFromWithStartAtReplacesElementsStartingAtSrcStart) {
  HandleScope scope(thread_);
  List dst(&scope, runtime_.newList());
  Tuple dst_tuple(&scope, runtime_.newMutableTuple(5));
  dst.setItems(*dst_tuple);
  dst.setNumItems(5);
  List src(&scope, listFromRange(0, 5));
  dst.replaceFromWithStartAt(0, *src, 2, 2);
  EXPECT_PYLIST_EQ(dst, {2, 3, Value::none(), Value::none(), Value::none()});
}

TEST_F(ListTest, ReplaceFromWithStartAtWithSelfNoop) {
  HandleScope scope(thread_);
  List dst(&scope, listFromRange(0, 5));
  dst.replaceFromWithStartAt(0, *dst, 2, 0);
  EXPECT_PYLIST_EQ(dst, {0, 1, 2, 3, 4});
}

TEST_F(ListTest, ReplaceFromWithStartAtWithSelfBackward) {
  HandleScope scope(thread_);
  List dst(&scope, listFromRange(0, 5));
  dst.replaceFromWithStartAt(0, *dst, 2, 2);
  EXPECT_PYLIST_EQ(dst, {2, 3, 2, 3, 4});
}

TEST_F(ListTest, ReplaceFromWithStartAtWithSelfForward) {
  HandleScope scope(thread_);
  List dst(&scope, listFromRange(0, 5));
  dst.replaceFromWithStartAt(2, *dst, 2, 0);
  EXPECT_PYLIST_EQ(dst, {0, 1, 0, 1, 4});
}

TEST_F(MutableTupleTest, NoneFillTupleFillsTupleWithNone) {
  HandleScope scope(thread_);
  MutableTuple tuple(&scope, runtime_.newMutableTuple(3));
  tuple.atPut(0, SmallInt::fromWord(0));
  tuple.atPut(1, SmallInt::fromWord(1));
  tuple.atPut(2, SmallInt::fromWord(2));
  tuple.fill(NoneType::object());
  EXPECT_EQ(tuple.at(0), NoneType::object());
  EXPECT_EQ(tuple.at(1), NoneType::object());
  EXPECT_EQ(tuple.at(2), NoneType::object());
}

TEST_F(MutableTupleTest, ReplaceFromWithReplacesElementsStartingAtZero) {
  HandleScope scope(thread_);
  MutableTuple dst(&scope, runtime_.newMutableTuple(5));
  List src(&scope, listFromRange(0, 5));
  Tuple src_items(&scope, src.items());
  dst.replaceFromWith(0, *src_items, 2);
  EXPECT_TRUE(isIntEqualsWord(dst.at(0), 0));
  EXPECT_TRUE(isIntEqualsWord(dst.at(1), 1));
  EXPECT_TRUE(dst.at(2).isNoneType());
  EXPECT_TRUE(dst.at(3).isNoneType());
  EXPECT_TRUE(dst.at(4).isNoneType());
}

TEST_F(MutableTupleTest, ReplaceFromWithReplacesElementsStartingInMiddle) {
  HandleScope scope(thread_);
  MutableTuple dst(&scope, runtime_.newMutableTuple(5));
  List src(&scope, listFromRange(0, 5));
  Tuple src_items(&scope, src.items());
  dst.replaceFromWith(1, *src_items, 2);
  EXPECT_TRUE(dst.at(0).isNoneType());
  EXPECT_TRUE(isIntEqualsWord(dst.at(1), 0));
  EXPECT_TRUE(isIntEqualsWord(dst.at(2), 1));
  EXPECT_TRUE(dst.at(3).isNoneType());
  EXPECT_TRUE(dst.at(4).isNoneType());
}

TEST_F(MutableTupleTest, ReplaceFromWithCopiesZeroElements) {
  HandleScope scope(thread_);
  MutableTuple dst(&scope, runtime_.newMutableTuple(5));
  List src(&scope, listFromRange(0, 5));
  Tuple src_items(&scope, src.items());
  dst.replaceFromWith(0, *src_items, 0);
  EXPECT_TRUE(dst.at(0).isNoneType());
  EXPECT_TRUE(dst.at(1).isNoneType());
  EXPECT_TRUE(dst.at(2).isNoneType());
  EXPECT_TRUE(dst.at(3).isNoneType());
  EXPECT_TRUE(dst.at(4).isNoneType());
}

TEST_F(MutableTupleTest, ReplaceFromWithCopiesEveryElementFromSrc) {
  HandleScope scope(thread_);
  MutableTuple dst(&scope, runtime_.newMutableTuple(5));
  List src(&scope, listFromRange(0, 5));
  Tuple src_items(&scope, src.items());
  dst.replaceFromWith(0, *src_items, 5);
  EXPECT_TRUE(isIntEqualsWord(dst.at(0), 0));
  EXPECT_TRUE(isIntEqualsWord(dst.at(1), 1));
  EXPECT_TRUE(isIntEqualsWord(dst.at(2), 2));
  EXPECT_TRUE(isIntEqualsWord(dst.at(3), 3));
  EXPECT_TRUE(isIntEqualsWord(dst.at(4), 4));
}

TEST_F(MutableTupleTest, ReplaceFromWithStartAtWithSelfNoop) {
  HandleScope scope(thread_);
  List dst_list(&scope, listFromRange(0, 5));
  MutableTuple dst(&scope, dst_list.items());
  dst.replaceFromWithStartAt(0, *dst, 2, 0);
  EXPECT_PYLIST_EQ(dst_list, {0, 1, 2, 3, 4});
}

TEST_F(MutableTupleTest, ReplaceFromWithStartAtWithSelfBackward) {
  HandleScope scope(thread_);
  List dst_list(&scope, listFromRange(0, 5));
  MutableTuple dst(&scope, dst_list.items());
  dst.replaceFromWithStartAt(0, *dst, 2, 2);
  EXPECT_PYLIST_EQ(dst_list, {2, 3, 2, 3, 4});
}

TEST_F(MutableTupleTest, ReplaceFromWithStartAtWithSelfForward) {
  HandleScope scope(thread_);
  List dst_list(&scope, listFromRange(0, 5));
  MutableTuple dst(&scope, dst_list.items());
  dst.replaceFromWithStartAt(2, *dst, 2, 0);
  EXPECT_PYLIST_EQ(dst_list, {0, 1, 0, 1, 4});
}

TEST_F(MutableTupleTest,
       ReplaceFromWithStartAtReplacesElementsStartingAtSrcStart) {
  HandleScope scope(thread_);
  MutableTuple dst(&scope, runtime_.newMutableTuple(5));
  List src_list(&scope, listFromRange(0, 5));
  Tuple src(&scope, src_list.items());
  dst.replaceFromWithStartAt(0, *src, 2, 2);
  EXPECT_TRUE(isIntEqualsWord(dst.at(0), 2));
  EXPECT_TRUE(isIntEqualsWord(dst.at(1), 3));
  EXPECT_TRUE(dst.at(2).isNoneType());
  EXPECT_TRUE(dst.at(3).isNoneType());
  EXPECT_TRUE(dst.at(4).isNoneType());
}

TEST(ErrorTest, ErrorIsError) {
  EXPECT_TRUE(Error::error().isError());

  EXPECT_TRUE(Error::exception().isError());
  EXPECT_TRUE(Error::exception().isErrorException());

  EXPECT_TRUE(Error::notFound().isError());
  EXPECT_TRUE(Error::notFound().isErrorNotFound());

  EXPECT_TRUE(Error::noMoreItems().isError());
  EXPECT_TRUE(Error::noMoreItems().isErrorNoMoreItems());

  EXPECT_TRUE(Error::outOfMemory().isError());
  EXPECT_TRUE(Error::outOfMemory().isErrorOutOfMemory());

  EXPECT_TRUE(Error::outOfBounds().isError());
  EXPECT_TRUE(Error::outOfBounds().isErrorOutOfBounds());
}

TEST(ErrorTest, ErrorHasCorrectKind) {
  EXPECT_EQ(Error::error().kind(), ErrorKind::kNone);
  EXPECT_EQ(Error::exception().kind(), ErrorKind::kException);
  EXPECT_EQ(Error::notFound().kind(), ErrorKind::kNotFound);
  EXPECT_EQ(Error::noMoreItems().kind(), ErrorKind::kNoMoreItems);
  EXPECT_EQ(Error::outOfMemory().kind(), ErrorKind::kOutOfMemory);
  EXPECT_EQ(Error::outOfBounds().kind(), ErrorKind::kOutOfBounds);
}

}  // namespace py
