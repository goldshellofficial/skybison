#include "gtest/gtest.h"

#include "Python.h"
#include "capi-fixture.h"
#include "capi-testing.h"

namespace python {

using namespace testing;

using ExceptionsExtensionApiTest = ExtensionApi;

TEST_F(ExceptionsExtensionApiTest, GettingCauseWithoutSettingItReturnsNull) {
  PyRun_SimpleString("a = TypeError()");
  PyObjectPtr exc(moduleGet("__main__", "a"));
  PyObjectPtr cause(PyException_GetCause(exc));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(cause, nullptr);
}

TEST_F(ExceptionsExtensionApiTest, GettingCauseAfterSetReturnsSameObject) {
  PyRun_SimpleString("a = TypeError()");
  PyObjectPtr exc(moduleGet("__main__", "a"));
  PyObjectPtr str(PyUnicode_FromString(""));
  // Since SetCause steals a reference, but we want to keept the object around
  // we need to incref it before passing it to the function
  Py_INCREF(str);
  PyException_SetCause(exc, str);
  PyObjectPtr cause(PyException_GetCause(exc));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(cause, str);
}

TEST_F(ExceptionsExtensionApiTest, SettingCauseWithNullSetsCauseToNull) {
  PyRun_SimpleString("a = TypeError()");
  PyObjectPtr exc(moduleGet("__main__", "a"));
  PyException_SetCause(exc, PyUnicode_FromString(""));
  PyException_SetCause(exc, nullptr);
  PyObjectPtr cause(PyException_GetCause(exc));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(cause, nullptr);
}

TEST_F(ExceptionsExtensionApiTest, GettingContextWithoutSettingItReturnsNull) {
  PyRun_SimpleString("a = TypeError()");
  PyObjectPtr exc(moduleGet("__main__", "a"));
  PyObjectPtr context(PyException_GetContext(exc));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(context, nullptr);
}

TEST_F(ExceptionsExtensionApiTest, GettingContextAfterSetReturnsSameObject) {
  PyRun_SimpleString("a = TypeError()");
  PyObjectPtr exc(moduleGet("__main__", "a"));
  PyObjectPtr str(PyUnicode_FromString(""));
  // Since SetContext steals a reference, but we want to keept the object around
  // we need to incref it before passing it to the function
  Py_INCREF(str);
  PyException_SetContext(exc, str);
  PyObjectPtr context(PyException_GetContext(exc));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(context, str);
}

TEST_F(ExceptionsExtensionApiTest, SettingContextWithNullSetsContextToNull) {
  PyRun_SimpleString("a = TypeError()");
  PyObjectPtr exc(moduleGet("__main__", "a"));
  PyException_SetContext(exc, PyUnicode_FromString(""));
  PyException_SetContext(exc, nullptr);
  PyObjectPtr context(PyException_GetContext(exc));
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_EQ(context, nullptr);
}

}  // namespace python
