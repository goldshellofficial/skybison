// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <cmath>

#include "Python.h"
#include "gtest/gtest.h"

#include "capi-fixture.h"
#include "capi-testing.h"

namespace py {
namespace testing {

using LongExtensionApiTest = ExtensionApi;

TEST_F(LongExtensionApiTest, GCDWithSameNumberReturnsSameNumber) {
  PyObjectPtr dividend(PyLong_FromLong(3));
  PyObjectPtr divisor(PyLong_FromLong(3));
  EXPECT_EQ(PyLong_AsLong(_PyLong_GCD(dividend, divisor)), 3);
}

TEST_F(LongExtensionApiTest, GCDWithDifferentNumbersReturnsGCD) {
  PyObjectPtr dividend(PyLong_FromLong(3));
  PyObjectPtr divisor(PyLong_FromLong(6));
  EXPECT_EQ(PyLong_AsLong(_PyLong_GCD(dividend, divisor)), 3);
}

TEST_F(LongExtensionApiTest, GCDWithOneNegativeReturnsPositive) {
  PyObjectPtr dividend(PyLong_FromLong(-3));
  PyObjectPtr divisor(PyLong_FromLong(3));
  EXPECT_EQ(PyLong_AsLong(_PyLong_GCD(dividend, divisor)), 3);
}

TEST_F(LongExtensionApiTest, GCDWithBothNegativeReturnsPositive) {
  PyObjectPtr dividend(PyLong_FromLong(-1));
  PyObjectPtr divisor(PyLong_FromLong(-2));
  EXPECT_EQ(PyLong_AsLong(_PyLong_GCD(dividend, divisor)), 1);
}

TEST_F(LongExtensionApiTest, GCDWithZeroAndThreeReturnsThree) {
  PyObjectPtr dividend(PyLong_FromLong(0));
  PyObjectPtr divisor(PyLong_FromLong(3));
  EXPECT_EQ(PyLong_AsLong(_PyLong_GCD(dividend, divisor)), 3);
}

TEST_F(LongExtensionApiTest, GCDWithThreeAndZeroReturnsThree) {
  PyObjectPtr dividend(PyLong_FromLong(3));
  PyObjectPtr divisor(PyLong_FromLong(0));
  EXPECT_EQ(PyLong_AsLong(_PyLong_GCD(dividend, divisor)), 3);
}
TEST_F(LongExtensionApiTest, GCDWithZeroandNegativeReturnsOne) {
  PyObjectPtr dividend(PyLong_FromLong(0));
  PyObjectPtr divisor(PyLong_FromLong(-1));
  EXPECT_EQ(PyLong_AsLong(_PyLong_GCD(dividend, divisor)), 1);
}

TEST_F(LongExtensionApiTest, GCDWithsSameLargeIntsReturnsSame) {
  PyObjectPtr dividend(PyLong_FromString("7FFFFFFFFFFFFFFF", nullptr, 16));
  PyObjectPtr divisor(PyLong_FromString("7FFFFFFFFFFFFFFF", nullptr, 16));
  PyObjectPtr expected(PyLong_FromString("7FFFFFFFFFFFFFFF", nullptr, 16));
  int res =
      PyObject_RichCompareBool(_PyLong_GCD(dividend, divisor), expected, Py_EQ);
  EXPECT_EQ(res, 1);
}

TEST_F(LongExtensionApiTest, GCDWithLargeIntsReturnsGCD) {
  PyObjectPtr dividend(PyLong_FromString("7FFFFFFFFFFFFFFF", nullptr, 16));
  PyObjectPtr divisor(PyLong_FromString("FFFFFFFFFFFFFFFE", nullptr, 16));
  PyObjectPtr expected(PyLong_FromString("7FFFFFFFFFFFFFFF", nullptr, 16));
  int res =
      PyObject_RichCompareBool(_PyLong_GCD(dividend, divisor), expected, Py_EQ);
  EXPECT_EQ(res, 1);
}

TEST_F(LongExtensionApiTest, GCDWithLargeIntsReturnsSmallIntGCD) {
  PyObjectPtr dividend(PyLong_FromString("FFFFFFFFFFFFFFFE", nullptr, 16));
  PyObjectPtr divisor(PyLong_FromString("10000000000000002", nullptr, 16));
  EXPECT_EQ(PyLong_AsLong(_PyLong_GCD(dividend, divisor)), 2);
}

TEST_F(LongExtensionApiTest, CheckWithIntReturnsTrue) {
  PyObjectPtr pylong(PyLong_FromLong(10));
  EXPECT_TRUE(PyLong_Check(pylong));
  EXPECT_TRUE(PyLong_CheckExact(pylong));

  pylong = PyLong_FromLongLong(10);
  EXPECT_TRUE(PyLong_Check(pylong));
  EXPECT_TRUE(PyLong_CheckExact(pylong));

  pylong = PyLong_FromUnsignedLong(10);
  EXPECT_TRUE(PyLong_Check(pylong));
  EXPECT_TRUE(PyLong_CheckExact(pylong));

  pylong = PyLong_FromUnsignedLongLong(10);
  EXPECT_TRUE(PyLong_Check(pylong));
  EXPECT_TRUE(PyLong_CheckExact(pylong));

  pylong = PyLong_FromSsize_t(10);
  EXPECT_TRUE(PyLong_Check(pylong));
  EXPECT_TRUE(PyLong_CheckExact(pylong));
}

TEST_F(LongExtensionApiTest, CheckWithIntSubclass) {
  PyRun_SimpleString(R"(
class X(int): pass
x = X()
)");
  PyObjectPtr x(mainModuleGet("x"));
  EXPECT_TRUE(PyLong_Check(x));
  EXPECT_FALSE(PyLong_CheckExact(x));
}

TEST_F(LongExtensionApiTest, CheckExactWithBoolReturnsFalse) {
  EXPECT_TRUE(PyLong_Check(Py_False));
  EXPECT_TRUE(PyLong_Check(Py_True));
  EXPECT_FALSE(PyLong_CheckExact(Py_False));
  EXPECT_FALSE(PyLong_CheckExact(Py_True));
}

TEST_F(LongExtensionApiTest, CheckWithTypeReturnsFalse) {
  PyObjectPtr pylong(PyLong_FromLong(10));
  PyObject* type = reinterpret_cast<PyObject*>(Py_TYPE(pylong));
  EXPECT_FALSE(PyLong_Check(type));
  EXPECT_FALSE(PyLong_CheckExact(type));
}

TEST_F(LongExtensionApiTest, AsDoubleWithNullRaisesSystemError) {
  EXPECT_EQ(PyLong_AsDouble(nullptr), -1.0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_SystemError));
}

TEST_F(LongExtensionApiTest, AsDoubleWithNonIntRaisesTypeError) {
  PyObjectPtr obj(PyList_New(0));
  EXPECT_EQ(PyLong_AsDouble(obj), -1.0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest, AsDoubleWithSmallIntReturnsDouble) {
  PyObjectPtr obj(PyLong_FromLong(10));
  EXPECT_EQ(PyLong_AsDouble(obj), 10.0);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(LongExtensionApiTest, AsDoubleWithNegativeIntReturnsDouble) {
  PyObjectPtr obj(PyLong_FromLong(-40));
  EXPECT_EQ(PyLong_AsDouble(obj), -40.0);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(LongExtensionApiTest, AsDoubleWithLargeIntReturnsDouble) {
  unsigned char bytes[9] = {1};
  double expected = std::pow(2, 64);
  PyObjectPtr obj(_PyLong_FromByteArray(bytes, sizeof(bytes), false, false));
  EXPECT_EQ(PyLong_AsDouble(obj), expected);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(LongExtensionApiTest, AsDoubleWithIntSubclassReturnsDouble) {
  PyRun_SimpleString(R"(
class X(int): pass
x = X(42)
)");
  PyObjectPtr x(mainModuleGet("x"));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(PyLong_AsDouble(x), 42.0);
}

TEST_F(LongExtensionApiTest, AsDoubleWithOverflowRaisesOverflowError) {
  unsigned char bytes[129] = {1};
  PyObjectPtr obj(_PyLong_FromByteArray(bytes, sizeof(bytes), false, false));
  EXPECT_EQ(PyLong_AsDouble(obj), -1.0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
}

TEST_F(LongExtensionApiTest, AsIntWithNullRaisesSystemError) {
  ASSERT_EQ(_PyLong_AsInt(nullptr), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_SystemError));
}

TEST_F(LongExtensionApiTest, AsIntWithNonIntegerRaisesTypeError) {
  ASSERT_EQ(_PyLong_AsInt(Py_None), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest, AsIntWithLongMaxRaisesOverflowError) {
  PyObjectPtr num(PyLong_FromLongLong(static_cast<long long>(INT_MAX) + 1));
  ASSERT_EQ(_PyLong_AsInt(num), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
}

TEST_F(LongExtensionApiTest, AsIntWithIntSubclassReturnsInt) {
  PyRun_SimpleString(R"(
class X(int): pass
x = X(42)
)");
  PyObjectPtr x(mainModuleGet("x"));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(_PyLong_AsInt(x), 42);
}

TEST_F(LongExtensionApiTest, AsIntWithInvalidDunderIntRaisesTypeError) {
  PyRun_SimpleString(R"(
class X:
  def __int__(self): return ""
x = X()
)");
  PyObjectPtr x(mainModuleGet("x"));
  ASSERT_EQ(_PyLong_AsInt(x), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest, AsIntWithValidDunderIntReturnsInt) {
  PyRun_SimpleString(R"(
class X:
    def __int__(self): return 42
x = X()
)");
  PyObjectPtr x(mainModuleGet("x"));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(_PyLong_AsInt(x), 42);
}

TEST_F(LongExtensionApiTest, AsLongWithNullReturnsNegative) {
  long res = PyLong_AsLong(nullptr);
  EXPECT_EQ(res, -1);

  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_SystemError));
}

TEST_F(LongExtensionApiTest, AsLongWithNonIntegerReturnsNegative) {
  long res = PyLong_AsLong(Py_None);
  EXPECT_EQ(res, -1);
  EXPECT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest, AsLongWithIntSubclassReturnsLong) {
  PyRun_SimpleString(R"(
class X(int): pass
x = X(42)
)");
  PyObjectPtr x(mainModuleGet("x"));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(PyLong_AsLong(x), 42);
}

TEST_F(LongExtensionApiTest, AsLongWithInvalidDunderInt) {
  PyRun_SimpleString(R"(
class X:
    def __int__(self):
        return "not an int"
x = X()
)");
  PyObjectPtr x(mainModuleGet("x"));
  EXPECT_EQ(PyLong_AsLong(x), -1l);
  EXPECT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest, AsLongWithValidDunderInt) {
  PyRun_SimpleString(R"(
class X:
    def __int__(self):
        return -7
x = X()
)");
  PyObjectPtr x(mainModuleGet("x"));
  EXPECT_EQ(PyLong_AsLong(x), -7);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(LongExtensionApiTest, AsLongWithBoolReturnsLong) {
  EXPECT_EQ(PyLong_AsLong(Py_True), 1);
  EXPECT_EQ(PyLong_AsLong(Py_False), 0);
}

TEST_F(LongExtensionApiTest, FromStringReturnsLong) {
  PyObjectPtr long0(PyLong_FromString("1", nullptr, 10));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyLong_CheckExact(long0));
  EXPECT_EQ(PyLong_AsSsize_t(long0), 1);

  PyObjectPtr long1(PyLong_FromString("1000", nullptr, 10));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyLong_CheckExact(long1));
  EXPECT_EQ(PyLong_AsSsize_t(long1), 1000);

  PyObjectPtr long2(PyLong_FromString("100", nullptr, 2));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyLong_CheckExact(long2));
  EXPECT_EQ(PyLong_AsSsize_t(long2), 4);
}

TEST_F(LongExtensionApiTest, FromStringWithInvalidIntRaisesValueError) {
  EXPECT_EQ(PyLong_FromString("foo", nullptr, 10), nullptr);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_ValueError));
}

TEST_F(LongExtensionApiTest, FromLongReturnsLong) {
  const int val = 10;
  PyObjectPtr pylong(PyLong_FromLong(val));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyLong_CheckExact(pylong));

  EXPECT_EQ(PyLong_AsLong(pylong), val);
  EXPECT_EQ(PyLong_AsLongLong(pylong), val);
  EXPECT_EQ(PyLong_AsSsize_t(pylong), val);

  auto const val2 = std::numeric_limits<long>::min();
  PyObjectPtr pylong2(PyLong_FromLong(val2));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyLong_CheckExact(pylong2));
  EXPECT_EQ(PyLong_AsLong(pylong2), val2);

  auto const val3 = std::numeric_limits<long>::max();
  PyObjectPtr pylong3(PyLong_FromLong(val3));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyLong_CheckExact(pylong3));
  EXPECT_EQ(PyLong_AsLong(pylong3), val3);
}

TEST_F(LongExtensionApiTest, FromUnsignedReturnsLong) {
  auto const ulmax = std::numeric_limits<unsigned long>::max();
  PyObjectPtr pylong(PyLong_FromUnsignedLong(ulmax));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyLong_CheckExact(pylong));
  EXPECT_EQ(PyLong_AsUnsignedLong(pylong), ulmax);
  EXPECT_EQ(PyLong_AsUnsignedLongLong(pylong), ulmax);
  EXPECT_EQ(PyLong_AsSize_t(pylong), ulmax);

  auto const ullmax = std::numeric_limits<unsigned long long>::max();
  PyObjectPtr pylong2(PyLong_FromUnsignedLongLong(ullmax));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyLong_CheckExact(pylong2));
  EXPECT_EQ(PyLong_AsUnsignedLongLong(pylong2), ullmax);

  auto const uval = 1234UL;
  PyObjectPtr pylong3(PyLong_FromUnsignedLong(uval));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyLong_CheckExact(pylong3));
  EXPECT_EQ(PyLong_AsUnsignedLong(pylong3), uval);
}

static PyObject* lshift(long num, long shift) {
  PyObject* num_obj = PyLong_FromLong(num);
  PyObject* shift_obj = PyLong_FromLong(shift);
  PyObject* result = PyNumber_Lshift(num_obj, shift_obj);
  Py_DECREF(num_obj);
  Py_DECREF(shift_obj);
  return result;
}

TEST_F(LongExtensionApiTest, NumBitsWithZeroReturnsZero) {
  PyObjectPtr num(PyLong_FromLong(0));
  EXPECT_EQ(_PyLong_NumBits(num), size_t{0});
}

TEST_F(LongExtensionApiTest, NumBitsWithOneReturnsOne) {
  PyObjectPtr num(PyLong_FromLong(1));
  EXPECT_EQ(_PyLong_NumBits(num), size_t{1});
}

TEST_F(LongExtensionApiTest, NumBitsWithNegativeOneReturnsOne) {
  PyObjectPtr num(PyLong_FromLong(-1));
  EXPECT_EQ(_PyLong_NumBits(num), size_t{1});
}

TEST_F(LongExtensionApiTest, NumBitsWithTwoReturnsTwo) {
  PyObjectPtr num(PyLong_FromLong(2));
  EXPECT_EQ(_PyLong_NumBits(num), size_t{2});
}

TEST_F(LongExtensionApiTest, NumBitsWithNegativeTwoReturnsTwo) {
  PyObjectPtr num(PyLong_FromLong(-2));
  EXPECT_EQ(_PyLong_NumBits(num), size_t{2});
}

TEST_F(LongExtensionApiTest, NumBitsWithThreeReturnsTwo) {
  PyObjectPtr num(PyLong_FromLong(3));
  EXPECT_EQ(_PyLong_NumBits(num), size_t{2});
}

TEST_F(LongExtensionApiTest, NumBitsWithNegativeThreeReturnsTwo) {
  PyObjectPtr num(PyLong_FromLong(-3));
  EXPECT_EQ(_PyLong_NumBits(num), size_t{2});
}

TEST_F(LongExtensionApiTest, NumBitsWithFourReturnsThree) {
  PyObjectPtr num(PyLong_FromLong(4));
  EXPECT_EQ(_PyLong_NumBits(num), size_t{3});
}

TEST_F(LongExtensionApiTest, NumBitsWithNegativeFourReturnsThree) {
  PyObjectPtr num(PyLong_FromLong(-4));
  EXPECT_EQ(_PyLong_NumBits(num), size_t{3});
}

TEST_F(LongExtensionApiTest, NumBitsCpythonTests) {
  PyObjectPtr i0(PyLong_FromLong(0x7fffL));
  EXPECT_EQ(_PyLong_NumBits(i0), size_t{15});
  PyObjectPtr negative_i0(PyLong_FromLong(-0x7fffL));
  EXPECT_EQ(_PyLong_NumBits(negative_i0), size_t{15});

  PyObjectPtr i1(PyLong_FromLong(0xffffL));
  EXPECT_EQ(_PyLong_NumBits(i1), size_t{16});
  PyObjectPtr negative_i1(PyLong_FromLong(-0xffffL));
  EXPECT_EQ(_PyLong_NumBits(negative_i1), size_t{16});

  PyObjectPtr i2(PyLong_FromLong(0xfffffffL));
  EXPECT_EQ(_PyLong_NumBits(i2), size_t{28});
  PyObjectPtr negative_i2(PyLong_FromLong(-0xfffffffL));
  EXPECT_EQ(_PyLong_NumBits(negative_i2), size_t{28});

  PyObjectPtr i3(PyLong_FromLong(PY_SSIZE_T_MAX));
  EXPECT_EQ(_PyLong_NumBits(i3), size_t{63});
  PyObjectPtr negative_i3(PyLong_FromLong(PY_SSIZE_T_MIN));
  EXPECT_EQ(_PyLong_NumBits(negative_i3), size_t{64});
}

TEST_F(LongExtensionApiTest, Overflow) {
  PyObjectPtr pylong(lshift(1, 100));

  EXPECT_EQ(PyLong_AsUnsignedLong(pylong), -1UL);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
  PyErr_Clear();

  EXPECT_EQ(PyLong_AsLong(pylong), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
  PyErr_Clear();

  EXPECT_EQ(PyLong_AsSsize_t(pylong), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
  PyErr_Clear();

  pylong = PyLong_FromLong(-123);
  EXPECT_EQ(PyLong_AsUnsignedLongLong(pylong), -1ULL);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
}

TEST_F(LongExtensionApiTest, AsLongAndOverflow) {
  auto const ulmax = std::numeric_limits<unsigned long>::max();
  auto const lmax = std::numeric_limits<long>::max();

  PyObjectPtr pylong(PyLong_FromUnsignedLong(ulmax));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  int overflow = 0;
  EXPECT_EQ(PyLong_AsLongAndOverflow(pylong, &overflow), -1);
  EXPECT_EQ(overflow, 1);
  overflow = 0;
  EXPECT_EQ(PyLong_AsLongLongAndOverflow(pylong, &overflow), -1);
  EXPECT_EQ(overflow, 1);

  pylong = PyLong_FromLong(lmax);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  overflow = 1;
  EXPECT_EQ(PyLong_AsLongAndOverflow(pylong, &overflow), lmax);
  EXPECT_EQ(overflow, 0);
  overflow = 1;
  EXPECT_EQ(PyLong_AsLongLongAndOverflow(pylong, &overflow), lmax);
  EXPECT_EQ(overflow, 0);

  pylong = lshift(-1, 100);
  overflow = 0;
  EXPECT_EQ(PyLong_AsLongAndOverflow(pylong, &overflow), -1);
  EXPECT_EQ(overflow, -1);
  overflow = 0;
  EXPECT_EQ(PyLong_AsLongLongAndOverflow(pylong, &overflow), -1);
  EXPECT_EQ(overflow, -1);
}

TEST_F(LongExtensionApiTest, AsUnsignedLongMaskWithMax) {
  auto const ulmax = std::numeric_limits<unsigned long>::max();
  PyObjectPtr pylong(PyLong_FromUnsignedLong(ulmax));
  EXPECT_EQ(PyLong_AsUnsignedLongMask(pylong), ulmax);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(PyLong_AsUnsignedLongLongMask(pylong), ulmax);
  EXPECT_EQ(PyErr_Occurred(), nullptr);

  auto const ullmax = std::numeric_limits<unsigned long long>::max();
  pylong = PyLong_FromUnsignedLongLong(ullmax);
  EXPECT_EQ(PyLong_AsUnsignedLongLongMask(pylong), ullmax);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(LongExtensionApiTest, AsUnsignedLongMaskWithLargeInt) {
  PyObjectPtr largeint(lshift(1, 100));
  PyObjectPtr pylong(PyNumber_Or(largeint, PyObjectPtr(PyLong_FromLong(123))));
  EXPECT_EQ(PyLong_AsUnsignedLongMask(pylong), 123UL);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(PyLong_AsUnsignedLongLongMask(pylong), 123ULL);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(LongExtensionApiTest, AsUnsignedLongMaskWithNegative) {
  PyObjectPtr pylong(PyLong_FromLong(-17));
  EXPECT_EQ(PyLong_AsUnsignedLongMask(pylong), -17UL);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(PyLong_AsUnsignedLongLongMask(pylong), -17ULL);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(LongExtensionApiTest, FromLongWithZeroReturnsZero) {
  PyObjectPtr pylong(PyLong_FromLong(0));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(pylong));
  EXPECT_EQ(PyLong_AsLong(pylong), 0);
}

TEST_F(LongExtensionApiTest,
       AsByteArrayUnsignedWithNegativeRaisesOverflowError) {
  PyObjectPtr num(PyLong_FromLong(-1));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[1];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, false), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
}

TEST_F(LongExtensionApiTest, AsByteArrayUnsignedWithZeroWritesZero) {
  PyObjectPtr num(PyLong_FromLong(0));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[1];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, false), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0);
}

TEST_F(LongExtensionApiTest, AsByteArrayUnsignedWritesMaxUnsignedByte) {
  PyObjectPtr num(PyLong_FromLong(0xff));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[1];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, false), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0xff);
}

TEST_F(LongExtensionApiTest,
       AsByteArrayUnsignedOverflowWritesByteAndRaisesOverflowError) {
  PyObjectPtr num(PyLong_FromLong(0x0100));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[1];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, true), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
  EXPECT_EQ(dst[0], 0x00);
}

TEST_F(LongExtensionApiTest, AsByteArrayUnsignedWritesBytesBigEndian) {
  PyObjectPtr num(PyLong_FromLong(0xface));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[3];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, false), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0x00);
  EXPECT_EQ(dst[1], 0xfa);
  EXPECT_EQ(dst[2], 0xce);
}

TEST_F(LongExtensionApiTest, AsByteArrayUnsigedWritesBytesLittleEndian) {
  PyObjectPtr num(PyLong_FromLong(0xface));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[3];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), true, false), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0xce);
  EXPECT_EQ(dst[1], 0xfa);
  EXPECT_EQ(dst[2], 0x00);
}

TEST_F(LongExtensionApiTest, AsByteArraySignedWritesMaxSignedByte) {
  PyObjectPtr num(PyLong_FromLong(0x7f));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[1];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, true), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0x7f);
}

TEST_F(LongExtensionApiTest, AsByteArraySignedWritesMinSignedByte) {
  PyObjectPtr num(PyLong_FromLong(-0x80));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[1];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, true), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0x80);
}

TEST_F(LongExtensionApiTest,
       AsByteArraySignedOverflowWritesByteAndRaisesOverflowError) {
  PyObjectPtr num(PyLong_FromLong(0x80));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[1];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, true), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
  EXPECT_EQ(dst[0], 0x80);
}

TEST_F(LongExtensionApiTest,
       AsByteArraySignedUnderflowWritesByteAndRaisesOverflowError) {
  PyObjectPtr num(PyLong_FromLong(-0x81));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[1];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, true), -1);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
  EXPECT_EQ(dst[0], 0x7f);
}

TEST_F(LongExtensionApiTest, AsByteArraySignedPositiveWritesBytesBigEndian) {
  PyObjectPtr num(PyLong_FromLong(0xface));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[3];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, true), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0x00);
  EXPECT_EQ(dst[1], 0xfa);
  EXPECT_EQ(dst[2], 0xce);
}

TEST_F(LongExtensionApiTest, AsByteArraySignedPositiveWritesBytesLittleEndian) {
  PyObjectPtr num(PyLong_FromLong(0xface));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[3];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), true, true), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0xce);
  EXPECT_EQ(dst[1], 0xfa);
  EXPECT_EQ(dst[2], 0x00);
}

TEST_F(LongExtensionApiTest, AsByteArraySignedNegativeWritesBytesBigEndian) {
  PyObjectPtr num(PyLong_FromLong(-0xface));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[3];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, true), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0xff);
  EXPECT_EQ(dst[1], 0x05);
  EXPECT_EQ(dst[2], 0x32);
}

TEST_F(LongExtensionApiTest, AsByteArraySignedNegativeWritesBytesLittleEndian) {
  PyObjectPtr num(PyLong_FromLong(-0xface));
  PyLongObject* obj = num.asLongObject();
  unsigned char dst[3];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), true, true), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0x32);
  EXPECT_EQ(dst[1], 0x05);
  EXPECT_EQ(dst[2], 0xff);
}

TEST_F(LongExtensionApiTest, AsByteArrayWithIntSubclassWritesBytes) {
  PyRun_SimpleString(R"(
class X(int): pass
x = X(0xface)
)");
  PyObjectPtr x(mainModuleGet("x"));
  PyLongObject* obj = x.asLongObject();
  unsigned char dst[3];
  ASSERT_EQ(_PyLong_AsByteArray(obj, dst, sizeof(dst), false, true), 0);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(dst[0], 0x00);
  EXPECT_EQ(dst[1], 0xfa);
  EXPECT_EQ(dst[2], 0xce);
}

TEST_F(LongExtensionApiTest, CopyWithIntReturnsInt) {
  PyObjectPtr x(PyLong_FromLong(42));
  PyObjectPtr result(_PyLong_Copy(x.asLongObject()));
  EXPECT_TRUE(PyLong_CheckExact(result));
  EXPECT_TRUE(isLongEqualsLong(result, 42));
}

TEST_F(LongExtensionApiTest, CopyWithIntSubclassReturnsExactInt) {
  PyRun_SimpleString(R"(
class X(int): pass
x = X(42)
)");
  PyObjectPtr x(mainModuleGet("x"));
  PyObjectPtr result(_PyLong_Copy(x.asLongObject()));
  EXPECT_TRUE(PyLong_CheckExact(result));
  EXPECT_TRUE(isLongEqualsLong(result, 42));
}

TEST_F(LongExtensionApiTest, DivmodNearWithNonIntDividendRaisesTypeError) {
  PyObjectPtr a(PyUnicode_FromString("not an int"));
  PyObjectPtr b(PyLong_FromLong(0));
  ASSERT_EQ(_PyLong_DivmodNear(a, b), nullptr);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest, DivmodNearWithNonIntDivisorRaisesTypeError) {
  PyObjectPtr a(PyLong_FromLong(0));
  PyObjectPtr b(PyUnicode_FromString("not an int"));
  ASSERT_EQ(_PyLong_DivmodNear(a, b), nullptr);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest, DivmodNearWithZeroDivisorRaisesZeroDivisionError) {
  PyObjectPtr a(PyLong_FromLong(0));
  PyObjectPtr b(PyLong_FromLong(0));
  ASSERT_EQ(_PyLong_DivmodNear(a, b), nullptr);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_ZeroDivisionError));
}

TEST_F(LongExtensionApiTest, DivmodNearRoundsToEven) {
  PyObjectPtr a(PyLong_FromLong(44));
  PyObjectPtr b(PyLong_FromLong(8));
  PyObjectPtr result(_PyLong_DivmodNear(a, b));
  ASSERT_TRUE(PyTuple_CheckExact(result));
  ASSERT_EQ(PyTuple_Size(result), 2);

  PyObject* quotient = PyTuple_GetItem(result, 0);
  ASSERT_TRUE(PyLong_CheckExact(quotient));
  EXPECT_EQ(PyLong_AsLong(quotient), 6);

  PyObject* remainder = PyTuple_GetItem(result, 1);
  ASSERT_TRUE(PyLong_CheckExact(remainder));
  EXPECT_EQ(PyLong_AsLong(remainder), -4);
}

TEST_F(LongExtensionApiTest, DivmodNearWithNegativeDividendReturnsTuple) {
  PyObjectPtr a(PyLong_FromLong(-43));
  PyObjectPtr b(PyLong_FromLong(5));
  PyObjectPtr result(_PyLong_DivmodNear(a, b));
  ASSERT_TRUE(PyTuple_CheckExact(result));
  ASSERT_EQ(PyTuple_Size(result), 2);

  PyObject* quotient = PyTuple_GetItem(result, 0);
  ASSERT_TRUE(PyLong_CheckExact(quotient));
  EXPECT_EQ(PyLong_AsLong(quotient), -9);

  PyObject* remainder = PyTuple_GetItem(result, 1);
  ASSERT_TRUE(PyLong_CheckExact(remainder));
  EXPECT_EQ(PyLong_AsLong(remainder), 2);
}

TEST_F(LongExtensionApiTest, DivmodNearWithNegativeDivisorReturnsTuple) {
  PyObjectPtr a(PyLong_FromLong(43));
  PyObjectPtr b(PyLong_FromLong(-5));
  PyObjectPtr result(_PyLong_DivmodNear(a, b));
  ASSERT_TRUE(PyTuple_CheckExact(result));
  ASSERT_EQ(PyTuple_Size(result), 2);

  PyObject* quotient = PyTuple_GetItem(result, 0);
  ASSERT_TRUE(PyLong_CheckExact(quotient));
  EXPECT_EQ(PyLong_AsLong(quotient), -9);

  PyObject* remainder = PyTuple_GetItem(result, 1);
  ASSERT_TRUE(PyLong_CheckExact(remainder));
  EXPECT_EQ(PyLong_AsLong(remainder), -2);
}

TEST_F(LongExtensionApiTest, DivmodNearWithNegativesReturnsTuple) {
  PyObjectPtr a(PyLong_FromLong(-43));
  PyObjectPtr b(PyLong_FromLong(-5));
  PyObjectPtr result(_PyLong_DivmodNear(a, b));
  ASSERT_TRUE(PyTuple_CheckExact(result));
  ASSERT_EQ(PyTuple_Size(result), 2);

  PyObject* quotient = PyTuple_GetItem(result, 0);
  ASSERT_TRUE(PyLong_CheckExact(quotient));
  EXPECT_EQ(PyLong_AsLong(quotient), 9);

  PyObject* remainder = PyTuple_GetItem(result, 1);
  ASSERT_TRUE(PyLong_CheckExact(remainder));
  EXPECT_EQ(PyLong_AsLong(remainder), 2);
}

TEST_F(LongExtensionApiTest, FromByteArrayWithZeroSizeReturnsZero) {
  PyObjectPtr num(_PyLong_FromByteArray(nullptr, 0, false, false));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(num));
  EXPECT_EQ(PyLong_AsLong(num), 0);
}

TEST_F(LongExtensionApiTest, FromByteArrayBigEndianUnsignedReturnsBytes) {
  const unsigned char source[] = {0x2c, 0xff, 0x00, 0x42};
  PyObjectPtr num(_PyLong_FromByteArray(source, 4, false, false));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(num));
  EXPECT_EQ(PyLong_AsLong(num), 0x2cff0042);
}

TEST_F(LongExtensionApiTest, FromByteArrayLittleEndianUnsignedReturnsBytes) {
  const unsigned char source[] = {0x2c, 0xff, 0x00, 0x42};
  PyObjectPtr num(_PyLong_FromByteArray(source, 4, true, false));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(num));
  EXPECT_EQ(PyLong_AsLong(num), 0x4200ff2c);
}

TEST_F(LongExtensionApiTest, FromByteArrayBigEndianSignedPositiveReturnsBytes) {
  const unsigned char source[] = {0x2c, 0xff, 0x00, 0x42};
  PyObjectPtr num(_PyLong_FromByteArray(source, 4, false, true));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(num));
  EXPECT_EQ(PyLong_AsLong(num), 0x2cff0042);
}

TEST_F(LongExtensionApiTest, FromByteArrayBigEndianSignedNegativeReturnsBytes) {
  const unsigned char source[] = {0xff, 0x2c, 0x00, 0x42};
  PyObjectPtr num(_PyLong_FromByteArray(source, 4, false, true));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(num));
  EXPECT_EQ(PyLong_AsLong(num), -0x00d3ffbe);
}

TEST_F(LongExtensionApiTest, FromByteArrayReturnsBytesWithSize) {
  const unsigned char source[] = {0x01, 0x02, 0x03};
  PyObjectPtr num(_PyLong_FromByteArray(source, 2, true, true));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(num));
  EXPECT_EQ(PyLong_AsLong(num), 0x0201);
}

TEST_F(LongExtensionApiTest, SignZeroReturnsZero) {
  PyObjectPtr zero(PyLong_FromLong(0));
  ASSERT_EQ(_PyLong_Sign(zero), 0);
}

TEST_F(LongExtensionApiTest, SignPositiveLongReturnsOne) {
  PyObjectPtr positive1(PyLong_FromLong(1));
  ASSERT_EQ(_PyLong_Sign(positive1), 1);
  PyObjectPtr positive1234(PyLong_FromLong(1234));
  ASSERT_EQ(_PyLong_Sign(positive1234), 1);
}

TEST_F(LongExtensionApiTest, SignNegativeReturnsNegativeOne) {
  PyObjectPtr negative1(PyLong_FromLong(-1));
  ASSERT_EQ(_PyLong_Sign(negative1), -1);
  PyObjectPtr negative5678(PyLong_FromLong(-5678));
  ASSERT_EQ(_PyLong_Sign(negative5678), -1);
}

TEST_F(LongExtensionApiTest, SignWithIntSubclassReturnsSign) {
  PyRun_SimpleString(R"(
class X(int): pass
a = X(-42)
b = X(0)
c = X(42)
)");
  PyObjectPtr a(mainModuleGet("a"));
  PyObjectPtr b(mainModuleGet("b"));
  PyObjectPtr c(mainModuleGet("c"));
  EXPECT_EQ(_PyLong_Sign(a), -1);
  EXPECT_EQ(_PyLong_Sign(b), 0);
  EXPECT_EQ(_PyLong_Sign(c), 1);
}

TEST_F(LongExtensionApiTest, FromVoidPtrReturnsLong) {
  unsigned long long max_as_int =
      std::numeric_limits<unsigned long long>::max();
  void* max_as_ptr = reinterpret_cast<void*>(max_as_int);
  PyObjectPtr pylong(PyLong_FromVoidPtr(max_as_ptr));
  EXPECT_EQ(PyLong_AsVoidPtr(pylong), max_as_ptr);
  EXPECT_EQ(PyLong_AsUnsignedLongLong(pylong), max_as_int);

  unsigned long long zero_as_int = 0ULL;
  void* zero_as_ptr = reinterpret_cast<void*>(zero_as_int);
  pylong = PyLong_FromVoidPtr(zero_as_ptr);
  EXPECT_EQ(PyLong_AsVoidPtr(pylong), zero_as_ptr);
  EXPECT_EQ(PyLong_AsUnsignedLongLong(pylong), zero_as_int);

  unsigned long long num_as_int = 1234ULL;
  void* num_as_ptr = reinterpret_cast<void*>(num_as_int);
  pylong = PyLong_FromVoidPtr(num_as_ptr);
  EXPECT_EQ(PyLong_AsVoidPtr(pylong), num_as_ptr);
  EXPECT_EQ(PyLong_AsUnsignedLongLong(pylong), num_as_int);
}

TEST_F(LongExtensionApiTest, FromDoubleReturnsLong) {
  PyObjectPtr pylong(PyLong_FromDouble(12.34));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  ASSERT_EQ(PyLong_Check(pylong), 1);
  EXPECT_EQ(PyLong_AsLong(pylong), 12);
}

TEST_F(LongExtensionApiTest, FromDoubleRaisesAndReturnsNull) {
  PyObjectPtr pylong(
      PyLong_FromDouble(std::numeric_limits<double>::infinity()));
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_EQ(pylong, nullptr);
}

TEST_F(LongExtensionApiTest, LshiftWithZeroReturnsZero) {
  PyObjectPtr result(_PyLong_Lshift(_PyLong_Zero, 10));
  EXPECT_TRUE(isLongEqualsLong(result, 0));
}

TEST_F(LongExtensionApiTest, LshiftWithNonzeroShiftsBits) {
  PyObjectPtr pos_result(_PyLong_Lshift(_PyLong_One, 10));
  EXPECT_TRUE(isLongEqualsLong(pos_result, 1024));

  PyObjectPtr neg(PyLong_FromLong(-5));
  PyObjectPtr neg_result(_PyLong_Lshift(neg, 4));
  EXPECT_TRUE(isLongEqualsLong(neg_result, -80));
}

TEST_F(LongExtensionApiTest, OneIsOne) {
  EXPECT_TRUE(isLongEqualsLong(_PyLong_One, 1));
}

TEST_F(LongExtensionApiTest, RshiftWithZeroReturnsZero) {
  PyObjectPtr result(_PyLong_Rshift(_PyLong_Zero, 10));
  EXPECT_TRUE(isLongEqualsLong(result, 0));
}

TEST_F(LongExtensionApiTest, RshiftWithNonzeroShiftsBits) {
  PyObjectPtr pos(PyLong_FromLong(257));
  PyObjectPtr pos_result(_PyLong_Rshift(pos, 3));
  EXPECT_TRUE(isLongEqualsLong(pos_result, 32));

  PyObjectPtr neg(PyLong_FromLong(-17));
  PyObjectPtr neg_result(_PyLong_Rshift(neg, 2));
  EXPECT_TRUE(isLongEqualsLong(neg_result, -5));
}

TEST_F(LongExtensionApiTest, SizeTConverterWithNonIntRaisesTypeError) {
  size_t ignored;
  PyObjectPtr tuple(PyTuple_New(0));
  EXPECT_EQ(_PyLong_Size_t_Converter(tuple, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest, SizeTConverterWithNegativeRaisesValueError) {
  size_t ignored;
  PyObjectPtr negative(PyLong_FromLong(-10));
  EXPECT_EQ(_PyLong_Size_t_Converter(negative, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_ValueError));
}

TEST_F(LongExtensionApiTest, SizeTConverterWithLargeIntRaisesOverflowError) {
  size_t ignored;
  PyObjectPtr large(PyLong_FromString("10000000000000002", nullptr, 16));
  EXPECT_EQ(_PyLong_Size_t_Converter(large, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
}

TEST_F(LongExtensionApiTest, SizeTConverterSetsSizeT) {
  size_t result;
  PyObjectPtr num(PyLong_FromLong(42));
  EXPECT_EQ(_PyLong_Size_t_Converter(num, &result), 1);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(result, static_cast<unsigned>(42));
}

TEST_F(LongExtensionApiTest, UnsignedIntConverterWithNonIntRaisesTypeError) {
  unsigned int ignored;
  PyObjectPtr tuple(PyTuple_New(0));
  EXPECT_EQ(_PyLong_UnsignedInt_Converter(tuple, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest, UnsignedIntConverterWithNegativeRaisesValueError) {
  unsigned int ignored;
  PyObjectPtr negative(PyLong_FromLong(-10));
  EXPECT_EQ(_PyLong_UnsignedInt_Converter(negative, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_ValueError));
}

TEST_F(LongExtensionApiTest,
       UnsignedIntConverterWithLargeIntRaisesOverflowError) {
  unsigned int ignored;
  PyObjectPtr large(PyLong_FromString("10000000000000002", nullptr, 16));
  EXPECT_EQ(_PyLong_UnsignedInt_Converter(large, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
}

TEST_F(LongExtensionApiTest, UnsignedIntConverterSetsUnsignedInt) {
  unsigned int result;
  PyObjectPtr num(PyLong_FromLong(42));
  EXPECT_EQ(_PyLong_UnsignedInt_Converter(num, &result), 1);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(result, static_cast<unsigned>(42));
}

TEST_F(LongExtensionApiTest, UnsignedLongConverterWithNonIntRaisesTypeError) {
  unsigned long ignored;
  PyObjectPtr tuple(PyTuple_New(0));
  EXPECT_EQ(_PyLong_UnsignedLong_Converter(tuple, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest,
       UnsignedLongConverterWithNegativeRaisesValueError) {
  unsigned long ignored;
  PyObjectPtr negative(PyLong_FromLong(-10));
  EXPECT_EQ(_PyLong_UnsignedLong_Converter(negative, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_ValueError));
}

TEST_F(LongExtensionApiTest,
       UnsignedLongConverterWithLargeIntRaisesOverflowError) {
  unsigned long ignored;
  PyObjectPtr large(PyLong_FromString("10000000000000002", nullptr, 16));
  EXPECT_EQ(_PyLong_UnsignedLong_Converter(large, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
}

TEST_F(LongExtensionApiTest, UnsignedLongConverterSetsUnsignedLong) {
  unsigned long result;
  PyObjectPtr num(PyLong_FromLong(42));
  EXPECT_EQ(_PyLong_UnsignedLong_Converter(num, &result), 1);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(result, static_cast<unsigned>(42));
}

TEST_F(LongExtensionApiTest,
       UnsignedLongLongConverterWithNonIntRaisesTypeError) {
  unsigned long long ignored;
  PyObjectPtr tuple(PyTuple_New(0));
  EXPECT_EQ(_PyLong_UnsignedLongLong_Converter(tuple, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest,
       UnsignedLongLongConverterWithNegativeRaisesValueError) {
  unsigned long long ignored;
  PyObjectPtr negative(PyLong_FromLong(-10));
  EXPECT_EQ(_PyLong_UnsignedLongLong_Converter(negative, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_ValueError));
}

TEST_F(LongExtensionApiTest,
       UnsignedLongLongConverterWithLargeIntRaisesOverflowError) {
  unsigned long long ignored;
  PyObjectPtr large(PyLong_FromString("10000000000000002", nullptr, 16));
  EXPECT_EQ(_PyLong_UnsignedLongLong_Converter(large, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
}

TEST_F(LongExtensionApiTest, UnsignedLongLongConverterSetsUnsignedLongLong) {
  unsigned long long result;
  PyObjectPtr num(PyLong_FromLong(42));
  EXPECT_EQ(_PyLong_UnsignedLongLong_Converter(num, &result), 1);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(result, static_cast<unsigned>(42));
}

TEST_F(LongExtensionApiTest, UnsignedShortConverterWithNonIntRaisesTypeError) {
  unsigned short ignored;
  PyObjectPtr tuple(PyTuple_New(0));
  EXPECT_EQ(_PyLong_UnsignedShort_Converter(tuple, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));
}

TEST_F(LongExtensionApiTest,
       UnsignedShortConverterWithNegativeRaisesValueError) {
  unsigned short ignored;
  PyObjectPtr negative(PyLong_FromLong(-10));
  EXPECT_EQ(_PyLong_UnsignedShort_Converter(negative, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_ValueError));
}

TEST_F(LongExtensionApiTest,
       UnsignedShortConverterWithLargeIntRaisesOverflowError) {
  unsigned short ignored;
  PyObjectPtr large(PyLong_FromString("10000000000000002", nullptr, 16));
  EXPECT_EQ(_PyLong_UnsignedShort_Converter(large, &ignored), 0);
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_OverflowError));
}

TEST_F(LongExtensionApiTest, UnsignedShortConverterSetsUnsignedShort) {
  unsigned short result;
  PyObjectPtr num(PyLong_FromLong(42));
  EXPECT_EQ(_PyLong_UnsignedShort_Converter(num, &result), 1);
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(result, static_cast<unsigned>(42));
}

TEST_F(LongExtensionApiTest, ZeroIsZero) {
  EXPECT_TRUE(isLongEqualsLong(_PyLong_Zero, 0));
}

}  // namespace testing
}  // namespace py
