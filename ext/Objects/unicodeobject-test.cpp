#include "gtest/gtest.h"

#include "Python.h"
#include "runtime/runtime.h"
#include "runtime/test-utils.h"

namespace python {

TEST(UnicodeObject, FromIdentifierReturnsUnicodeObject) {
  Runtime runtime;
  HandleScope scope;

  _Py_IDENTIFIER(__name__);
  const char* str = "__name__";
  PyObject* pyunicode = _PyUnicode_FromId(&PyId___name__);
  Handle<Object> string_obj(
      &scope, ApiHandle::fromPyObject(pyunicode)->asObject());
  ASSERT_TRUE(string_obj->isString());
  EXPECT_TRUE(String::cast(*string_obj)->equalsCString(str));
}

TEST(UnicodeObject, AsUTF8FromNonStringReturnsNull) {
  Runtime runtime;
  HandleScope scope;

  Handle<Object> integer_obj(&scope, runtime.newInteger(15));
  PyObject* pylong = runtime.asApiHandle(*integer_obj)->asPyObject();

  // Pass a non string object
  char* cstring = PyUnicode_AsUTF8AndSize(pylong, nullptr);
  EXPECT_EQ(nullptr, cstring);
}

TEST(UnicodeObject, AsUTF8WithNullSizeReturnsCString) {
  Runtime runtime;
  HandleScope scope;

  const char* str = "Some C String";
  Handle<String> string_obj(&scope, runtime.newStringFromCString(str));
  PyObject* pyunicode = runtime.asApiHandle(*string_obj)->asPyObject();

  // Pass a nullptr size
  char* cstring = PyUnicode_AsUTF8AndSize(pyunicode, nullptr);
  ASSERT_NE(nullptr, cstring);
  EXPECT_STREQ(str, cstring);
  std::free(cstring);
}

TEST(UnicodeObject, AsUTF8WithReferencedSizeReturnsCString) {
  Runtime runtime;
  HandleScope scope;

  const char* str = "Some C String";
  Handle<String> string_obj(&scope, runtime.newStringFromCString(str));
  PyObject* pyunicode = runtime.asApiHandle(*string_obj)->asPyObject();

  // Pass a size reference
  Py_ssize_t size = 0;
  char* cstring = PyUnicode_AsUTF8AndSize(pyunicode, &size);
  ASSERT_NE(nullptr, cstring);
  EXPECT_STREQ(str, cstring);
  EXPECT_EQ(size, strlen(str));
  std::free(cstring);
}

} // namespace python
