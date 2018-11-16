#include "gtest/gtest.h"

#include "Python.h"
#include "runtime/runtime.h"
#include "runtime/test-utils.h"

namespace python {

TEST(Object, PyNoneReturnsRuntimeNone) {
  Runtime runtime;
  HandleScope scope;

  // Test None
  PyObject* none = Py_None;
  Handle<Object> none_object(&scope, ApiHandle::fromPyObject(none)->asObject());
  EXPECT_TRUE(none_object->isNone());
}

TEST(Object, PyNoneIdentityIsEqual) {
  Runtime runtime;

  // Test Identitiy
  PyObject* none1 = Py_None;
  PyObject* none2 = Py_None;
  EXPECT_EQ(none1, none2);
}

} // namespace python
