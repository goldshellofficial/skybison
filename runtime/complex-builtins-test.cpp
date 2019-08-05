#include "gtest/gtest.h"

#include "complex-builtins.h"
#include "frame.h"
#include "object-builtins.h"
#include "runtime.h"
#include "test-utils.h"

namespace python {

using namespace testing;

using ComplexBuiltinsTest = RuntimeFixture;

TEST_F(ComplexBuiltinsTest, AddWithIntReturnsComplex) {
  HandleScope scope(thread_);
  Complex c(&scope, runtime_.newComplex(1, 2));
  Int i(&scope, runtime_.newInt(10));
  Object result_obj(&scope, runBuiltin(ComplexBuiltins::dunderAdd, c, i));
  ASSERT_FALSE(result_obj.isError());
  Complex result(&scope, *result_obj);
  EXPECT_EQ(result.real(), 11);
  EXPECT_EQ(result.imag(), 2);
}

TEST_F(ComplexBuiltinsTest, IntAddWithComplexReturnsComplex) {
  HandleScope scope(thread_);
  Frame* frame = thread_->currentFrame();
  Int i(&scope, runtime_.newInt(10));
  Complex c(&scope, runtime_.newComplex(1, 2));
  Object result_obj(
      &scope, Interpreter::binaryOperation(thread_, frame,
                                           Interpreter::BinaryOp::ADD, i, c));
  ASSERT_FALSE(result_obj.isError());
  Complex result(&scope, *result_obj);
  EXPECT_EQ(result.real(), 11);
  EXPECT_EQ(result.imag(), 2);
}

TEST_F(ComplexBuiltinsTest, NewWithNoArgsReturnsZero) {
  HandleScope scope(thread_);
  ASSERT_FALSE(
      runFromCStr(&runtime_, "result = complex.__new__(complex)").isError());
  Complex cmplx(&scope, mainModuleAt(&runtime_, "result"));
  EXPECT_EQ(cmplx.real(), 0);
  EXPECT_EQ(cmplx.imag(), 0);
}

TEST_F(ComplexBuiltinsTest, NewWithOneNumberArgReturnsComplexWithReal) {
  HandleScope scope(thread_);
  ASSERT_FALSE(
      runFromCStr(&runtime_, "result = complex.__new__(complex, 1)").isError());
  Complex cmplx(&scope, mainModuleAt(&runtime_, "result"));
  EXPECT_EQ(cmplx.real(), 1.0);
  EXPECT_EQ(cmplx.imag(), 0);
}

TEST_F(ComplexBuiltinsTest, NewWithTwoNumberArgReturnsComplexWithReal) {
  HandleScope scope(thread_);
  Object type(&scope, runtime_.typeAt(LayoutId::kComplex));
  Object int1(&scope, runtime_.newInt(1));
  Object int2(&scope, runtime_.newInt(2));
  Complex cmplx(&scope,
                runBuiltin(ComplexBuiltins::dunderNew, type, int1, int2));
  EXPECT_EQ(cmplx.real(), 1.0);
  EXPECT_EQ(cmplx.imag(), 2.0);
}

TEST_F(ComplexBuiltinsTest, NewWithComplexArgReturnsSameComplex) {
  HandleScope scope(thread_);
  ASSERT_FALSE(
      runFromCStr(&runtime_,
                  "result = complex.__new__(complex, complex(1.0, 2.0))")
          .isError());
  Complex cmplx(&scope, mainModuleAt(&runtime_, "result"));
  EXPECT_EQ(cmplx.real(), 1.0);
  EXPECT_EQ(cmplx.imag(), 2.0);
}

TEST_F(ComplexBuiltinsTest, DunderReprHasRealAndImag) {
  ASSERT_FALSE(
      runFromCStr(&runtime_, "result = repr(complex(1, 2))").isError());
  HandleScope scope(thread_);
  Object result(&scope, mainModuleAt(&runtime_, "result"));
  EXPECT_TRUE(isStrEqualsCStr(*result, "(1.0+2.0j)"));
}

TEST_F(ComplexBuiltinsTest, BuiltinBaseIsComplex) {
  HandleScope scope(thread_);
  Type complex_type(&scope, runtime_.typeAt(LayoutId::kComplex));
  EXPECT_EQ(complex_type.builtinBase(), LayoutId::kComplex);
}

}  // namespace python
