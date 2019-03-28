#include "gtest/gtest.h"

#include <limits>

#include "float-builtins.h"
#include "handles.h"
#include "int-builtins.h"
#include "objects.h"
#include "runtime.h"
#include "test-utils.h"

namespace python {

using namespace testing;

TEST(FloatBuiltinsTest, DunderMulWithDoubleReturnsDouble) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(2.0));
  Float right(&scope, runtime.newFloat(1.5));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderMul, left, right));
  ASSERT_TRUE(result.isFloat());
  EXPECT_EQ(RawFloat::cast(*result).value(), 3.0);
}

TEST(FloatBuiltinsTest, DunderMulWithSmallIntReturnsDouble) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(2.5));
  Int right(&scope, runtime.newInt(1));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderMul, left, right));
  ASSERT_TRUE(result.isFloat());
  EXPECT_EQ(RawFloat::cast(*result).value(), 2.5);
}

TEST(FloatBuiltinsTest, DunderMulWithNonFloatSelfRaisesTypeError) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope, NoneType::object());
  Float right(&scope, runtime.newFloat(1.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderMul, left, right));
  EXPECT_TRUE(raised(*result, LayoutId::kTypeError));
}

TEST(FloatBuiltinsTest, DunderMulWithNonFloatOtherReturnsNotImplemented) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(1.0));
  Object right(&scope, NoneType::object());
  Object result(&scope, runBuiltin(FloatBuiltins::dunderMul, left, right));
  EXPECT_TRUE(result.isNotImplementedType());
}

TEST(FloatBuiltinsTest, DunderNeWithInequalFloatsReturnsTrue) {
  Runtime runtime;
  runFromCStr(&runtime, "result = float.__ne__(12.2, 2.12)");
  EXPECT_EQ(moduleAt(&runtime, "__main__", "result"), Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderNeWithEqualFloatIntReturnsFalse) {
  Runtime runtime;
  runFromCStr(&runtime, "result = float.__ne__(34.0, 34)");
  EXPECT_EQ(moduleAt(&runtime, "__main__", "result"), Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderNeWithStringReturnsNotImplemented) {
  Runtime runtime;
  runFromCStr(&runtime, "result = float.__ne__(5.5, '')");
  EXPECT_TRUE(moduleAt(&runtime, "__main__", "result").isNotImplementedType());
}

TEST(FloatBuiltinsTest, DunderAbsZeroReturnsZero) {
  Runtime runtime;
  HandleScope scope;
  Float self(&scope, runtime.newFloat(0.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderAbs, self));
  ASSERT_TRUE(result.isFloat());
  EXPECT_EQ(RawFloat::cast(*result).value(), 0.0);
}

TEST(FloatBuiltinsTest, DunderAbsNegativeReturnsPositive) {
  Runtime runtime;
  HandleScope scope;
  Float self(&scope, runtime.newFloat(-1234.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderAbs, self));
  ASSERT_TRUE(result.isFloat());
  EXPECT_EQ(RawFloat::cast(*result).value(), 1234.0);
}

TEST(FloatBuiltinsTest, DunderAbsPositiveReturnsPositive) {
  Runtime runtime;
  HandleScope scope;
  Float self(&scope, runtime.newFloat(5678.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderAbs, self));
  ASSERT_TRUE(result.isFloat());
  EXPECT_EQ(RawFloat::cast(*result).value(), 5678.0);
}

TEST(FloatBuiltinsTest, BinaryAddDouble) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
a = 2.0
b = 1.5
c = a + b
)");

  Object c(&scope, moduleAt(&runtime, "__main__", "c"));
  ASSERT_TRUE(c.isFloat());
  EXPECT_EQ(RawFloat::cast(*c).value(), 3.5);
}

TEST(FloatBuiltinsTest, BinaryAddSmallInt) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
a = 2.5
b = 1
c = a + b
)");

  Object c(&scope, moduleAt(&runtime, "__main__", "c"));
  ASSERT_TRUE(c.isFloat());
  EXPECT_EQ(RawFloat::cast(*c).value(), 3.5);
}

TEST(FloatBuiltinsTest, AddWithNonFloatSelfRaisesTypeError) {
  const char* src = R"(
float.__add__(None, 1.0)
)";
  Runtime runtime;
  EXPECT_TRUE(
      raisedWithStr(runFromCStr(&runtime, src), LayoutId::kTypeError,
                    "'__add__' requires a 'float' object but got 'NoneType'"));
}

TEST(FloatBuiltinsTest, AddWithNonFloatOtherRaisesTypeError) {
  const char* src = R"(
1.0 + None
)";
  Runtime runtime;
  EXPECT_TRUE(raisedWithStr(runFromCStr(&runtime, src), LayoutId::kTypeError,
                            "float.__add__(NoneType) is not supported"));
}

TEST(FloatBuiltinsTest, DunderBoolWithZeroReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Float self(&scope, runtime.newFloat(0.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderBool, self));
  EXPECT_EQ(*result, Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderBoolWithNonZeroReturnsTrue) {
  Runtime runtime;
  HandleScope scope;
  Float self(&scope, runtime.newFloat(1234.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderBool, self));
  EXPECT_EQ(*result, Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderTrueDivWithDoubleReturnsDouble) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(3.0));
  Float right(&scope, runtime.newFloat(2.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderTrueDiv, left, right));
  ASSERT_TRUE(result.isFloat());
  EXPECT_EQ(RawFloat::cast(*result).value(), 1.5);
}

TEST(FloatBuiltinsTest, DunderTrueDivWithSmallIntReturnsDouble) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(3.0));
  Int right(&scope, runtime.newInt(2));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderTrueDiv, left, right));
  ASSERT_TRUE(result.isFloat());
  EXPECT_EQ(RawFloat::cast(*result).value(), 1.5);
}

TEST(FloatBuiltinsTest, DunderTrueDivWithNonFloatSelfRaisesTypeError) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope, NoneType::object());
  Float right(&scope, runtime.newFloat(1.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderTrueDiv, left, right));
  EXPECT_TRUE(raised(*result, LayoutId::kTypeError));
}

TEST(FloatBuiltinsTest, DunderTrueDivWithNonFloatOtherReturnsNotImplemented) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(1.0));
  Object right(&scope, NoneType::object());
  Object result(&scope, runBuiltin(FloatBuiltins::dunderTrueDiv, left, right));
  EXPECT_TRUE(result.isNotImplementedType());
}

TEST(FloatBuiltinsTest, DunderTrueDivWithZeroFloatRaisesZeroDivisionError) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(1.0));
  Float right(&scope, runtime.newFloat(0.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderTrueDiv, left, right));
  EXPECT_TRUE(raised(*result, LayoutId::kZeroDivisionError));
}

TEST(FloatBuiltinsTest, DunderTrueDivWithZeroSmallIntRaisesZeroDivisionError) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(1.0));
  Int right(&scope, runtime.newInt(0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderTrueDiv, left, right));
  EXPECT_TRUE(raised(*result, LayoutId::kZeroDivisionError));
}

TEST(FloatBuiltinsTest, DunderTrueDivWithZeroBoolRaisesZeroDivisionError) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(1.0));
  Bool right(&scope, RawBool::falseObj());
  Object result(&scope, runBuiltin(FloatBuiltins::dunderTrueDiv, left, right));
  EXPECT_TRUE(raised(*result, LayoutId::kZeroDivisionError));
}

TEST(FloatBuiltinsTest, DunderRtrueDivWithDoubleReturnsDouble) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(2.0));
  Float right(&scope, runtime.newFloat(3.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderRtrueDiv, left, right));
  ASSERT_TRUE(result.isFloat());
  EXPECT_EQ(RawFloat::cast(*result).value(), 1.5);
}

TEST(FloatBuiltinsTest, DunderRtrueDivWithSmallIntReturnsDouble) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(2.0));
  Int right(&scope, runtime.newInt(3));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderRtrueDiv, left, right));
  ASSERT_TRUE(result.isFloat());
  EXPECT_EQ(RawFloat::cast(*result).value(), 1.5);
}

TEST(FloatBuiltinsTest, DunderRtrueDivWithNonFloatSelfRaisesTypeError) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope, NoneType::object());
  Float right(&scope, runtime.newFloat(1.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderRtrueDiv, left, right));
  EXPECT_TRUE(raised(*result, LayoutId::kTypeError));
}

TEST(FloatBuiltinsTest, DunderRtrueDivWithNonFloatOtherReturnsNotImplemented) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(1.0));
  Object right(&scope, NoneType::object());
  Object result(&scope, runBuiltin(FloatBuiltins::dunderRtrueDiv, left, right));
  EXPECT_TRUE(result.isNotImplementedType());
}

TEST(FloatBuiltinsTest, DunderRtrueDivWithZeroFloatRaisesZeroDivisionError) {
  Runtime runtime;
  HandleScope scope;
  Float left(&scope, runtime.newFloat(0.0));
  Float right(&scope, runtime.newFloat(1.0));
  Object result(&scope, runBuiltin(FloatBuiltins::dunderRtrueDiv, left, right));
  EXPECT_TRUE(raised(*result, LayoutId::kZeroDivisionError));
}

TEST(FloatBuiltinsTest, BinarySubtractDouble) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
a = 2.0
b = 1.5
c = a - b
)");

  Object c(&scope, moduleAt(&runtime, "__main__", "c"));
  ASSERT_TRUE(c.isFloat());
  EXPECT_EQ(RawFloat::cast(*c).value(), 0.5);
}

TEST(FloatBuiltinsTest, BinarySubtractSmallInt) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
a = 2.5
b = 1
c = a - b
)");

  Object c(&scope, moduleAt(&runtime, "__main__", "c"));
  ASSERT_TRUE(c.isFloat());
  EXPECT_EQ(RawFloat::cast(*c).value(), 1.5);
}

TEST(FloatBuiltinsTest, DunderNewWithNoArgsReturnsZero) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
a = float.__new__(float)
)");

  Object a(&scope, moduleAt(&runtime, "__main__", "a"));
  ASSERT_TRUE(a.isFloat());
  EXPECT_EQ(RawFloat::cast(*a).value(), 0.0);
}

TEST(FloatBuiltinsTest, DunderNewWithFloatArgReturnsSameValue) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
a = float.__new__(float, 1.0)
)");

  Object a(&scope, moduleAt(&runtime, "__main__", "a"));
  ASSERT_TRUE(a.isFloat());
  EXPECT_EQ(RawFloat::cast(*a).value(), 1.0);
}

TEST(FloatBuiltinsTest, DunderNewWithUserDefinedTypeReturnsFloat) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
class Foo:
  def __float__(self):
    return 1.0
a = float.__new__(float, Foo())
)");

  Float a(&scope, moduleAt(&runtime, "__main__", "a"));
  EXPECT_EQ(a.value(), 1.0);
}

TEST(FloatBuiltinsTest, DunderNewWithStringReturnsFloat) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
a = float.__new__(float, "1.5")
)");

  Float a(&scope, moduleAt(&runtime, "__main__", "a"));
  EXPECT_EQ(a.value(), 1.5);
}

TEST(FloatBuiltinsTest, FloatSubclassReturnsFloat) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
class SubFloat(float):
  def __new__(self, value):
    self.foo = 3
    return super().__new__(self, value)
subfloat = SubFloat(1.5)
subfloat_foo = subfloat.foo
)");

  // Check that it's a subtype of float
  Object subfloat(&scope, moduleAt(&runtime, "__main__", "subfloat"));
  ASSERT_FALSE(subfloat.isFloat());
  ASSERT_TRUE(runtime.isInstanceOfFloat(*subfloat));

  UserFloatBase user_float(&scope, *subfloat);
  Object float_value(&scope, user_float.floatValue());
  ASSERT_TRUE(float_value.isFloat());
  EXPECT_EQ(Float::cast(*float_value).value(), 1.5);

  Object foo_attr(&scope, moduleAt(&runtime, "__main__", "subfloat_foo"));
  EXPECT_TRUE(isIntEqualsWord(*foo_attr, 3));
}

TEST(FloatBuiltins, FloatSubclassKeepsFloatInMro) {
  const char* src = R"(
class Test(float):
  pass
)";
  Runtime runtime;
  HandleScope scope;
  runFromCStr(&runtime, src);
  Object value(&scope, moduleAt(&runtime, "__main__", "Test"));
  ASSERT_TRUE(value.isType());

  Type type(&scope, *value);
  ASSERT_TRUE(type.mro().isTuple());

  Tuple mro(&scope, type.mro());
  ASSERT_EQ(mro.length(), 3);
  EXPECT_EQ(mro.at(0), *type);
  EXPECT_EQ(mro.at(1), runtime.typeAt(LayoutId::kFloat));
  EXPECT_EQ(mro.at(2), runtime.typeAt(LayoutId::kObject));
}

TEST(FloatBuiltinsTest, DunderNewWithStringOfHugeNumberReturnsInf) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
a = float.__new__(float, "1.18973e+4932")
b = float.__new__(float, "-1.18973e+4932")

)");
  Float a(&scope, moduleAt(&runtime, "__main__", "a"));
  Float b(&scope, moduleAt(&runtime, "__main__", "b"));
  EXPECT_EQ(a.value(), std::numeric_limits<double>::infinity());
  EXPECT_EQ(b.value(), -std::numeric_limits<double>::infinity());
}

TEST(FloatBuiltinsTest, SubWithNonFloatSelfRaisesTypeError) {
  const char* src = R"(
float.__sub__(None, 1.0)
)";
  Runtime runtime;
  EXPECT_TRUE(
      raisedWithStr(runFromCStr(&runtime, src), LayoutId::kTypeError,
                    "'__sub__' requires a 'float' object but got 'NoneType'"));
}

TEST(FloatBuiltinsTest, PowFloatAndFloat) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
base = 2.0
x = base ** 4.0
)");
  Float result(&scope, moduleAt(&runtime, "__main__", "x"));
  EXPECT_EQ(result.value(), 16.0);
}

TEST(FloatBuiltinsTest, PowFloatAndInt) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
base = 2.0
x = base ** 4
)");
  Float result(&scope, moduleAt(&runtime, "__main__", "x"));
  EXPECT_EQ(result.value(), 16.0);
}

TEST(FloatBuiltinsTest, InplacePowFloatAndFloat) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
x = 2.0
x **= 4.0
)");
  Float result(&scope, moduleAt(&runtime, "__main__", "x"));
  EXPECT_EQ(result.value(), 16.0);
}

TEST(FloatBuiltinsTest, InplacePowFloatAndInt) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
x = 2.0
x **= 4
)");
  Float result(&scope, moduleAt(&runtime, "__main__", "x"));
  EXPECT_EQ(result.value(), 16.0);
}

TEST(FloatBuiltinsTest, FloatNewWithDunderFloatReturnsStringRaisesTypeError) {
  const char* src = R"(
class Foo:
  def __float__(self):
    return "non-float"
a = float.__new__(Foo)
)";
  Runtime runtime;
  EXPECT_TRUE(raisedWithStr(runFromCStr(&runtime, src), LayoutId::kTypeError,
                            "float.__new__(X): X is not a subtype of float"));
}

TEST(FloatBuiltinsTest, DunderNewWithInvalidStringRaisesValueError) {
  Runtime runtime;

  EXPECT_TRUE(raisedWithStr(runFromCStr(&runtime, R"(
a = float.__new__(float, "abc")
)"),
                            LayoutId::kValueError,
                            "could not convert string to float"));
}

TEST(FloatBuiltinsTest, SubWithNonFloatOtherRaisesTypeError) {
  const char* src = R"(
1.0 - None
)";
  Runtime runtime;
  EXPECT_TRUE(raisedWithStr(runFromCStr(&runtime, src), LayoutId::kTypeError,
                            "float.__sub__(NoneType) is not supported"));
}

TEST(FloatBuiltinsTest, DunderEqWithFloatsReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object nan(&scope,
             runtime.newFloat(std::numeric_limits<double>::quiet_NaN()));
  Object f0(&scope, runtime.newFloat(1.0));
  Object f1(&scope, runtime.newFloat(-42.5));
  Object zero(&scope, runtime.newFloat(0.0));
  Object neg_zero(&scope, runtime.newFloat(-0.0));
  Object null(&scope, runtime.newInt(0));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, f0, f0), Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, f0, f1), Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, nan, nan), Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, zero, neg_zero),
            Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, neg_zero, null),
            Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderEqWithSmallIntExactReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(31.0));
  Object float1(&scope, runtime.newFloat(31.125));
  Object int0(&scope, runtime.newFloat(31));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, float0, int0), Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, float1, int0),
            Bool::falseObj());
  word mantissa_max = (word{1} << (kDoubleMantissaBits + 1)) - 1;
  Object max_float(&scope, runtime.newFloat(static_cast<double>(mantissa_max)));
  Object max_int(&scope, runtime.newFloat(mantissa_max));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, max_float, max_int),
            Bool::trueObj());
  Object neg_max_float(&scope,
                       runtime.newFloat(static_cast<double>(-mantissa_max)));
  Object neg_max_int(&scope, runtime.newInt(-mantissa_max));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, neg_max_float, neg_max_int),
            Bool::trueObj());

  word big0 = word(1) << (kDoubleMantissaBits + 2);
  ASSERT_EQ(static_cast<double>(big0), static_cast<double>(big0) + 1.0);
  Object big0_float(&scope, runtime.newFloat(static_cast<double>(big0)));
  Int big0_int(&scope, runtime.newInt(big0));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, big0_float, big0_int),
            Bool::trueObj());

  word big1 = (word(1) << (kDoubleMantissaBits + 1)) | (word(1) << 11);
  ASSERT_EQ(static_cast<double>(big1), static_cast<double>(big1) + 1.0);
  Object big1_float(&scope, runtime.newFloat(static_cast<double>(big1)));
  Int big1_int(&scope, runtime.newInt(big1));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, big1_float, big1_int),
            Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderEqWithSmallIntInexactReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  word big = (word(1) << (kDoubleMantissaBits + 4)) + 3;
  ASSERT_EQ(static_cast<double>(big), static_cast<double>(big) + 3.0);
  Object big_float(&scope, runtime.newFloat(static_cast<double>(big)));
  Int big_int(&scope, runtime.newInt(big));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, big_float, big_int),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderEqWithLargeIntExactReturnsTrue) {
  Runtime runtime;
  HandleScope scope;
  Object int0(&scope, runtime.newIntWithDigits({0, 1}));
  Object float0(&scope, runtime.newFloat(std::strtod("0x1p64", nullptr)));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, float0, int0), Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderEqWithLargeIntInexactReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object int0(&scope, runtime.newIntWithDigits({0x800, 1}));
  Object float0(&scope, runtime.newFloat(std::strtod("0x1p64", nullptr)));
  ASSERT_EQ(RawFloat::cast(runBuiltin(IntBuiltins::dunderFloat, int0)).value(),
            RawFloat::cast(*float0).value());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, float0, int0),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderEqWithNonFiniteFloatIntReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object nan(&scope,
             runtime.newFloat(std::numeric_limits<double>::quiet_NaN()));
  Object inf(&scope, runtime.newFloat(std::numeric_limits<double>::infinity()));
  Object int0(&scope, runtime.newInt(7));
  std::unique_ptr<uword[]> digits(new uword[100]());
  digits[99] = 1;
  Object int1(&scope, runtime.newIntWithDigits(View<uword>(digits.get(), 100)));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, nan, int0), Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, inf, int0), Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, nan, int1), Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, inf, int1), Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderEqWithFloatOverflowingIntReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(8.25));
  std::unique_ptr<uword[]> digits(new uword[100]());
  digits[99] = 1;
  Object int0(&scope, runtime.newIntWithDigits(View<uword>(digits.get(), 100)));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderEq, float0, int0),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderFloatWithFloatLiteralReturnsSameObject) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, "a = (7.0).__float__()");
  Object a(&scope, moduleAt(&runtime, "__main__", "a"));
  ASSERT_TRUE(a.isFloat());
  EXPECT_EQ(RawFloat::cast(*a).value(), 7.0);
}

TEST(FloatBuiltinsTest, DunderFloatFromFloatClassReturnsSameValue) {
  Runtime runtime;
  HandleScope scope;

  Float a_float(&scope, runtime.newFloat(7.0));
  Object a(&scope, runBuiltin(FloatBuiltins::dunderFloat, a_float));
  ASSERT_TRUE(a.isFloat());
  EXPECT_EQ(RawFloat::cast(*a).value(), 7.0);
}

TEST(FloatBuiltinsTest, DunderFloatWithFloatSubclassReturnsSameValue) {
  Runtime runtime;
  HandleScope scope;

  runFromCStr(&runtime, R"(
class FloatSub(float):
  pass
a = FloatSub(1.0).__float__())");
  Object a(&scope, moduleAt(&runtime, "__main__", "a"));
  ASSERT_TRUE(a.isFloat());
  EXPECT_EQ(RawFloat::cast(*a).value(), 1.0);
}

TEST(FloatBuiltinsTest, DunderFloatWithNonFloatReturnsError) {
  Runtime runtime;
  HandleScope scope;

  Int i(&scope, runtime.newInt(1));
  Object i_res(&scope, runBuiltin(FloatBuiltins::dunderFloat, i));
  EXPECT_TRUE(raised(*i_res, LayoutId::kTypeError));
}

TEST(FloatBuiltinsTest, DunderGeWithFloatReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(1.7));
  Object float1(&scope, runtime.newFloat(0.2));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, float1),
            Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, float0),
            Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float1, float0),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderGeWithIntSelfNanReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope,
              runtime.newFloat(std::numeric_limits<double>::quiet_NaN()));
  Object right(&scope, runtime.newIntWithDigits({0, 1}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, left, right), Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderGeWithNonFloatReturnsNotImplemented) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope, runtime.newFloat(0.0));
  Object right(&scope, Str::empty());
  EXPECT_TRUE(
      runBuiltin(FloatBuiltins::dunderGe, left, right).isNotImplementedType());
}

TEST(FloatBuiltinsTest, DunderGeWithSmallIntReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(5.0));
  Object int0(&scope, runtime.newInt(4));
  Object int1(&scope, runtime.newInt(5));
  Object int2(&scope, runtime.newInt(6));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, int0), Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, int1), Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, int2),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderGeWithSmallIntExactReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(44.));
  Object int0(&scope, runtime.newInt(44));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, int0), Bool::trueObj());
  Object float1(&scope, runtime.newFloat(-3.));
  Object int1(&scope, runtime.newInt(1));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float1, int1),
            Bool::falseObj());

  Object float2(&scope, runtime.newFloat(0x20000000000000));
  Object int2(&scope, runtime.newInt(0x20000000000000));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float2, int2), Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderGeWithSmallIntInexactReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(0x20000000000001));
  Object int0(&scope, runtime.newInt(0x20000000000001));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, int0),
            Bool::falseObj());
  Object float1(&scope, runtime.newFloat(0x20000000000003));
  Object int1(&scope, runtime.newInt(0x20000000000003));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float1, int1), Bool::trueObj());
  Object float2(&scope, runtime.newFloat(0x100000000000011));
  Object int2(&scope, runtime.newInt(0x100000000000011));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float2, int2),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderGeWithLargeIntDifferingSignReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(-1.0));
  Object int0(&scope, runtime.newIntWithDigits({0, 1}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, int0),
            Bool::falseObj());
  Object float1(&scope, runtime.newFloat(1.0));
  Object int1(&scope, runtime.newIntWithDigits({0, kMaxUword}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float1, int1), Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderGeWithLargeIntExactEqualsReturnsTrue) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(std::strtod("0x1p64", nullptr)));
  Object int0(&scope, runtime.newIntWithDigits({0, 1}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, int0), Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderGeWithLargeIntRoundingDownReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(std::strtod("0x1p64", nullptr)));
  Object int0(&scope, runtime.newIntWithDigits({1, 1}));
  ASSERT_EQ(RawFloat::cast(runBuiltin(IntBuiltins::dunderFloat, int0)).value(),
            RawFloat::cast(*float0).value());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, int0),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderGeWithLargeIntRoundingUpReturnsTrue) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(std::strtod("0x1p64", nullptr)));
  Object int0(&scope, runtime.newIntWithDigits({kMaxUword, 0}));
  ASSERT_EQ(RawFloat::cast(runBuiltin(IntBuiltins::dunderFloat, int0)).value(),
            RawFloat::cast(*float0).value());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGe, float0, int0), Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderGtWithFloatReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(8.3));
  Object float1(&scope, runtime.newFloat(1.7));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGt, float0, float1),
            Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGt, float0, float0),
            Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGt, float1, float0),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderGtWithIntSelfNanReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope,
              runtime.newFloat(std::numeric_limits<double>::quiet_NaN()));
  Object right(&scope, runtime.newIntWithDigits({0, 1}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGt, left, right), Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderGtWithNonFloatReturnsNotImplemented) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope, runtime.newFloat(0.0));
  Object right(&scope, Str::empty());
  EXPECT_TRUE(
      runBuiltin(FloatBuiltins::dunderGt, left, right).isNotImplementedType());
}

TEST(FloatBuiltinsTest, DunderGtWithSmallIntReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(5.0));
  Object int0(&scope, runtime.newInt(4));
  Object int1(&scope, runtime.newInt(5));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGt, float0, int0), Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderGt, float0, int1),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderLeWithFloatReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(13.1));
  Object float1(&scope, runtime.newFloat(9.4));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLe, float0, float1),
            Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLe, float0, float0),
            Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLe, float1, float0),
            Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderLeWithIntSelfNanReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope,
              runtime.newFloat(std::numeric_limits<double>::quiet_NaN()));
  Object right(&scope, runtime.newIntWithDigits({0, 1}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLe, left, right), Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderLeWithNonFloatReturnsNotImplemented) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope, runtime.newFloat(0.0));
  Object right(&scope, Str::empty());
  EXPECT_TRUE(
      runBuiltin(FloatBuiltins::dunderLe, left, right).isNotImplementedType());
}

TEST(FloatBuiltinsTest, DunderLeWithSmallIntReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(4.0));
  Object int0(&scope, runtime.newInt(4));
  Object int1(&scope, runtime.newInt(3));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLe, float0, int0), Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLe, float0, int1),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderLeWithBoolReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(1.0));
  Object b_false(&scope, Bool::falseObj());
  Object b_true(&scope, Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLe, float0, b_false),
            Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLe, float0, b_true),
            Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderLtWithFloatReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(-7.3));
  Object float1(&scope, runtime.newFloat(1.25));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, float1),
            Bool::trueObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, float0),
            Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float1, float0),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderLtWithIntSelfNanReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope,
              runtime.newFloat(std::numeric_limits<double>::quiet_NaN()));
  Object right(&scope, runtime.newIntWithDigits({0, 1}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, left, right), Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderLtWithNonFloatReturnsNotImplemented) {
  Runtime runtime;
  HandleScope scope;
  Object left(&scope, runtime.newFloat(0.0));
  Object right(&scope, Str::empty());
  EXPECT_TRUE(
      runBuiltin(FloatBuiltins::dunderLt, left, right).isNotImplementedType());
}

TEST(FloatBuiltinsTest, DunderLtWithSmallIntReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(4.5));
  Object int0(&scope, runtime.newInt(4));
  Object int1(&scope, runtime.newInt(5));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, int0),
            Bool::falseObj());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, int1), Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderLtWithSmallIntExactReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(44.));
  Object int0(&scope, runtime.newInt(44));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, int0),
            Bool::falseObj());
  Object float1(&scope, runtime.newFloat(-3.));
  Object int1(&scope, runtime.newInt(1));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float1, int1), Bool::trueObj());

  Object float2(&scope, runtime.newFloat(0x20000000000000));
  Object int2(&scope, runtime.newInt(0x20000000000000));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float2, int2),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderLtWithSmallIntInexactReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(0x20000000000001));
  Object int0(&scope, runtime.newInt(0x20000000000001));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, int0), Bool::trueObj());
  Object float1(&scope, runtime.newFloat(0x20000000000003));
  Object int1(&scope, runtime.newInt(0x20000000000003));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float1, int1),
            Bool::falseObj());
  Object float2(&scope, runtime.newFloat(0x100000000000011));
  Object int2(&scope, runtime.newInt(0x100000000000011));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float2, int2), Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderLtWithLargeIntDifferingSignReturnsBool) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(-1.0));
  Object int0(&scope, runtime.newIntWithDigits({0, 1}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, int0), Bool::trueObj());
  Object float1(&scope, runtime.newFloat(1.0));
  Object int1(&scope, runtime.newIntWithDigits({0, kMaxUword}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float1, int1),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderLtWithLargeIntExactEqualsReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(std::strtod("0x1p64", nullptr)));
  Object int0(&scope, runtime.newIntWithDigits({0, 1}));
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, int0),
            Bool::falseObj());
}

TEST(FloatBuiltinsTest, DunderLtWithLargeIntRoundingDownReturnsTrue) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(std::strtod("0x1p64", nullptr)));
  Object int0(&scope, runtime.newIntWithDigits({1, 1}));
  ASSERT_EQ(RawFloat::cast(runBuiltin(IntBuiltins::dunderFloat, int0)).value(),
            RawFloat::cast(*float0).value());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, int0), Bool::trueObj());
}

TEST(FloatBuiltinsTest, DunderLtWithLargeIntRoundingUpReturnsFalse) {
  Runtime runtime;
  HandleScope scope;
  Object float0(&scope, runtime.newFloat(std::strtod("0x1p64", nullptr)));
  Object int0(&scope, runtime.newIntWithDigits({kMaxUword, 0}));
  ASSERT_EQ(RawFloat::cast(runBuiltin(IntBuiltins::dunderFloat, int0)).value(),
            RawFloat::cast(*float0).value());
  EXPECT_EQ(runBuiltin(FloatBuiltins::dunderLt, float0, int0),
            Bool::falseObj());
}

}  // namespace python
