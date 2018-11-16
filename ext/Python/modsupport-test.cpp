#include "capi-fixture.h"
#include "capi-testing.h"

namespace python {

using ModSupportExtensionApiTest = ExtensionApi;

TEST_F(ModSupportExtensionApiTest, AddObjectAddsToModule) {
  PyModuleDef def = {
      PyModuleDef_HEAD_INIT,
      "mymodule",
  };

  PyObject* module = PyModule_Create(&def);
  ASSERT_NE(module, nullptr);

  PyObject* obj = PyList_New(1);
  int myobj = PyModule_AddObject(module, "myobj", obj);
  ASSERT_NE(myobj, -1);

  PyRun_SimpleString(R"(
import mymodule
x = mymodule.myobj
)");

  PyObject* x = testing::moduleGet("__main__", "x");
  ASSERT_TRUE(PyList_CheckExact(x));
}

TEST_F(ModSupportExtensionApiTest, AddObjectWithNullNameFails) {
  PyModuleDef def = {
      PyModuleDef_HEAD_INIT,
      "mymodule",
  };

  PyObject* module = PyModule_Create(&def);
  ASSERT_NE(module, nullptr);

  PyObject* obj = PyList_New(1);
  int result = PyModule_AddObject(module, nullptr, obj);
  ASSERT_EQ(result, -1);
}

TEST_F(ModSupportExtensionApiTest, RepeatedAddObjectOverwritesValue) {
  PyModuleDef def = {
      PyModuleDef_HEAD_INIT,
      "mymodule",
  };

  PyObject* module = PyModule_Create(&def);
  ASSERT_NE(module, nullptr);

  PyObject* listobj = PyList_New(1);
  int myobj = PyModule_AddObject(module, "myobj", listobj);
  ASSERT_NE(myobj, -1);

  PyObject* tupleobj = PyTuple_New(1);
  myobj = PyModule_AddObject(module, "myobj", tupleobj);
  ASSERT_NE(myobj, -1);

  PyRun_SimpleString(R"(
import mymodule
x = mymodule.myobj
)");

  PyObject* x = testing::moduleGet("__main__", "x");
  ASSERT_FALSE(PyList_CheckExact(x));
  ASSERT_TRUE(PyTuple_CheckExact(x));
}

}  // namespace python
