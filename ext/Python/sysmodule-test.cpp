// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <string>

#include "Python.h"
#include "gtest/gtest.h"

#include "capi-fixture.h"
#include "capi-testing.h"

namespace py {
namespace testing {

using SysModuleExtensionApiTest = ExtensionApi;

TEST_F(SysModuleExtensionApiTest, GetObjectWithNonExistentNameReturnsNull) {
  EXPECT_EQ(PySys_GetObject("foo_bar_not_a_real_name"), nullptr);
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(SysModuleExtensionApiTest, GetObjectReturnsValueFromSysModule) {
  PyRun_SimpleString(R"(
import sys
sys.foo = 'bar'
)");
  PyObject* result = PySys_GetObject("foo");  // borrowed reference
  EXPECT_EQ(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(isUnicodeEqualsCStr(result, "bar"));
}

TEST_F(SysModuleExtensionApiTest, GetSizeOfPropagatesException) {
  PyRun_SimpleString(R"(
class C:
  def __sizeof__(self): raise Exception()
o = C()
)");
  PyObjectPtr object(mainModuleGet("o"));
  EXPECT_EQ(_PySys_GetSizeOf(object), static_cast<size_t>(-1));
  ASSERT_NE(PyErr_Occurred(), nullptr);
  EXPECT_TRUE(PyErr_ExceptionMatches(PyExc_Exception));
}

TEST_F(SysModuleExtensionApiTest, GetSizeOfReturnsDunderSizeOfPyro) {
  PyRun_SimpleString(R"(
class C:
  def __sizeof__(self): return 10
o = C()
)");
  PyObjectPtr object(mainModuleGet("o"));
  EXPECT_EQ(_PySys_GetSizeOf(object), size_t{10});
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(SysModuleExtensionApiTest, GetSizeOfWithIntSubclassReturnsIntPyro) {
  PyRun_SimpleString(R"(
class N(int): pass
class C:
  def __sizeof__(self): return N(10)
o = C()
)");
  PyObjectPtr object(mainModuleGet("o"));
  EXPECT_EQ(_PySys_GetSizeOf(object), size_t{10});
  EXPECT_EQ(PyErr_Occurred(), nullptr);
}

TEST_F(SysModuleExtensionApiTest, WriteStdout) {
  CaptureStdStreams streams;
  PySys_WriteStdout("Hello, %s!", "World");
  EXPECT_EQ(streams.out(), "Hello, World!");
  EXPECT_EQ(streams.err(), "");
}

TEST_F(
    SysModuleExtensionApiTest,
    WriteStdoutCallsSysStdoutWriteOnExceptionWritesToFallbackAndClearsError) {
  PyRun_SimpleString(R"(
import sys
x = 7
class C:
  def write(self, text):
    global x
    x = 42
    raise UserWarning()

sys.stdout = C()
)");
  CaptureStdStreams streams;
  PySys_WriteStdout("a");
  EXPECT_EQ(streams.out(), "a");
  EXPECT_EQ(streams.err(), "");
  ASSERT_EQ(PyErr_Occurred(), nullptr);
  PyObjectPtr x(mainModuleGet("x"));
  EXPECT_EQ(PyLong_AsLong(x), 42);
}

TEST_F(SysModuleExtensionApiTest, WriteStdoutWithSysStdoutNoneWritesToStdout) {
  PyRun_SimpleString(R"(
import sys
sys.stdout = None
)");
  CaptureStdStreams streams;
  PySys_WriteStdout("Hello");
  EXPECT_EQ(streams.out(), "Hello");
  EXPECT_EQ(streams.err(), "");
}

TEST_F(SysModuleExtensionApiTest, WriteStdoutWithoutSysStdoutWritesToStdout) {
  PyRun_SimpleString(R"(
import sys
del sys.stdout
)");
  CaptureStdStreams streams;
  PySys_WriteStdout("Konnichiwa\n");
  EXPECT_EQ(streams.out(), "Konnichiwa\n");
  EXPECT_EQ(streams.err(), "");
}

TEST_F(SysModuleExtensionApiTest, WriteStdoutTruncatesLongOutput) {
  const size_t max_out_len = 1000;
  std::string long_str;
  for (int i = 0; i < 100; i++) {
    long_str += "0123456789";
  }
  ASSERT_EQ(long_str.size(), max_out_len);

  CaptureStdStreams streams;
  PySys_WriteStdout("%s hello", long_str.c_str());
  EXPECT_EQ(streams.out(), long_str + "... truncated");
  EXPECT_EQ(streams.err(), "");
}

TEST_F(SysModuleExtensionApiTest, WriteStderr) {
  CaptureStdStreams streams;
  PySys_WriteStderr("2 + 2 = %d", 4);
  EXPECT_EQ(streams.out(), "");
  EXPECT_EQ(streams.err(), "2 + 2 = 4");
}

TEST_F(SysModuleExtensionApiTest,
       SetArgvWithEmptyArgvPopulatesSysArgvAndSysPathWithEmptyString) {
  wchar_t arg0[] = L"python";
  wchar_t* wargv[] = {arg0};
  PySys_SetArgv(0, wargv + 1);

  PyObject* argv = PySys_GetObject("argv");
  EXPECT_EQ(PyList_Size(argv), 1);
  EXPECT_TRUE(isUnicodeEqualsCStr(PyList_GetItem(argv, 0), ""));
  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, ""));
}

TEST_F(SysModuleExtensionApiTest,
       SetArgvWithScriptAndArgsPopulatesSysArgvWithScriptAndArgs) {
  wchar_t arg0[] = L"script.py";
  wchar_t arg1[] = L"3";
  wchar_t arg2[] = L"2";
  wchar_t* wargv[] = {arg0, arg1, arg2};
  PySys_SetArgv(3, wargv);
  PyObject* argv = PySys_GetObject("argv");
  EXPECT_EQ(PyList_Size(argv), 3);
  EXPECT_TRUE(isUnicodeEqualsCStr(PyList_GetItem(argv, 0), "script.py"));
  EXPECT_TRUE(isUnicodeEqualsCStr(PyList_GetItem(argv, 1), "3"));
  EXPECT_TRUE(isUnicodeEqualsCStr(PyList_GetItem(argv, 2), "2"));
}

TEST_F(SysModuleExtensionApiTest,
       SetArgvWithModuleArgInsertsWorkingDirectoryIntoSysPath) {
  wchar_t arg0[] = L"-m";
  wchar_t* wargv[] = {arg0};
  PySys_SetArgv(1, wargv);

  char cwd[PATH_MAX] = {0};
  ASSERT_NE(::getcwd(cwd, PATH_MAX), nullptr);

  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, cwd));
}

TEST_F(SysModuleExtensionApiTest,
       SetArgvWithCommandArgInsertsEmptyStringIntoSysPath) {
  wchar_t arg0[] = L"-c";
  wchar_t* wargv[] = {arg0};
  PySys_SetArgv(1, wargv);

  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, ""));
}

TEST_F(SysModuleExtensionApiTest,
       SetArgvWithAbsolutePathInsertsPathIntoSysPath) {
  TempDirectory tmpdir;
  std::string tmpfile = tmpdir.path() + "scriptfile.py";
  std::string touch = "touch " + tmpfile;
  int result = std::system(touch.c_str());
  ASSERT_EQ(result, 0);

  wchar_t arg0[] = L"python";
  wchar_t* arg1 = Py_DecodeLocale(tmpfile.c_str(), nullptr);
  wchar_t* wargv[] = {arg0, arg1};
  PySys_SetArgv(1, wargv + 1);

  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  char* abs_realpath = ::realpath(tmpdir.path().c_str(), nullptr);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, abs_realpath));

  PyMem_RawFree(arg1);
  std::free(abs_realpath);
}

TEST_F(SysModuleExtensionApiTest, SetArgvWithLocalPathAddsPathStringToSysPath) {
  TempDirectory tmpdir;
  char* cwd = ::getcwd(nullptr, 0);
  int result = ::chdir(tmpdir.path().c_str());
  ASSERT_EQ(result, 0);

  std::string tmpfile = "scriptfile.py";
  std::string touch = "touch " + tmpfile;
  result = std::system(touch.c_str());
  ASSERT_EQ(result, 0);

  wchar_t arg0[] = L"python";
  wchar_t* arg1 = Py_DecodeLocale(tmpfile.c_str(), nullptr);
  wchar_t* wargv[] = {arg0, arg1};
  PySys_SetArgv(1, wargv + 1);

  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  char* abs_path = ::realpath(tmpdir.path().c_str(), nullptr);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, abs_path));

  result = ::chdir(cwd);
  ASSERT_EQ(result, 0);
  PyMem_RawFree(arg1);
  std::free(abs_path);
  std::free(cwd);
}

TEST_F(SysModuleExtensionApiTest, SetArgvWithRelativePathAddsPathToSysPath) {
  TempDirectory tmpdir;
  char* cwd = ::getcwd(nullptr, 0);
  int result = ::chdir(tmpdir.path().c_str());
  ASSERT_EQ(result, 0);

  std::string relative_path = "./";
  std::string tmpfile = relative_path + "scriptfile.py";
  std::string touch = "touch " + tmpfile;
  result = std::system(touch.c_str());
  ASSERT_EQ(result, 0);

  wchar_t arg0[] = L"python";
  wchar_t* arg1 = Py_DecodeLocale(tmpfile.c_str(), nullptr);
  wchar_t* wargv[] = {arg0, arg1};
  PySys_SetArgv(1, wargv + 1);

  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  char* abs_path = ::realpath(relative_path.c_str(), nullptr);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, abs_path));

  result = ::chdir(cwd);
  ASSERT_EQ(result, 0);
  PyMem_RawFree(arg1);
  std::free(cwd);
  std::free(abs_path);
}

TEST_F(SysModuleExtensionApiTest, SetArgvWithRootPathInsertsRootIntoSysPath) {
  wchar_t arg0[] = L"python";
  wchar_t arg1[] = L"/root_script.py";
  wchar_t* wargv[] = {arg0, arg1};
  PySys_SetArgv(1, wargv + 1);

  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, "/"));
}

TEST_F(SysModuleExtensionApiTest,
       SetArgvWithLocalSymlinkInsertsPathIntoSysPath) {
  TempDirectory tmpdir;
  std::string tmpfile = tmpdir.path() + "scriptfile.py";
  std::string linkfile = tmpdir.path() + "scriptlink.py";
  std::string touch = "touch " + tmpfile;
  int result = std::system(touch.c_str());
  ASSERT_EQ(result, 0);
  std::string ln = "ln -s scriptfile.py " + linkfile;
  result = std::system(ln.c_str());
  ASSERT_EQ(result, 0);

  wchar_t arg0[] = L"python";
  wchar_t* arg1 = Py_DecodeLocale(linkfile.c_str(), nullptr);
  wchar_t* wargv[] = {arg0, arg1};
  PySys_SetArgv(1, wargv + 1);

  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  char* abs_realpath = ::realpath(tmpdir.path().c_str(), nullptr);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, abs_realpath));

  PyMem_RawFree(arg1);
  std::free(abs_realpath);
}

TEST_F(SysModuleExtensionApiTest,
       SetArgvWithAbsoluteSymlinkInsertsPathIntoSysPath) {
  TempDirectory tmpdir1;
  TempDirectory tmpdir2;
  std::string tmpfile = tmpdir1.path() + "scriptfile.py";
  std::string linkfile = tmpdir2.path() + "scriptlink.py";
  std::string touch = "touch " + tmpfile;
  int result = std::system(touch.c_str());
  ASSERT_EQ(result, 0);
  std::string ln = "ln -s " + tmpfile + " " + linkfile;
  result = std::system(ln.c_str());
  ASSERT_EQ(result, 0);

  wchar_t arg0[] = L"python";
  wchar_t* arg1 = Py_DecodeLocale(linkfile.c_str(), nullptr);
  wchar_t* wargv[] = {arg0, arg1};
  PySys_SetArgv(1, wargv + 1);

  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  char* abs_realpath = ::realpath(tmpdir1.path().c_str(), nullptr);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, abs_realpath));

  PyMem_RawFree(arg1);
  std::free(abs_realpath);
}

TEST_F(SysModuleExtensionApiTest,
       SetArgvWithRelativeSymlinkInsertsPathIntoSysPath) {
  TempDirectory tmpdir;
  std::string tmpfile = tmpdir.path() + "scriptfile.py";
  std::string linkfile = tmpdir.path() + "scriptlink.py";
  std::string touch = "touch " + tmpfile;
  int result = std::system(touch.c_str());
  ASSERT_EQ(result, 0);
  std::string ln = "ln -s ./scriptfile.py " + linkfile;
  result = std::system(ln.c_str());
  ASSERT_EQ(result, 0);

  wchar_t arg0[] = L"python";
  wchar_t* arg1 = Py_DecodeLocale(linkfile.c_str(), nullptr);
  wchar_t* wargv[] = {arg0, arg1};
  PySys_SetArgv(1, wargv + 1);

  PyObject* sys_path = PySys_GetObject("path");
  PyObject* sys_path0 = PyList_GetItem(sys_path, 0);
  char* abs_realpath = ::realpath(tmpdir.path().c_str(), nullptr);
  EXPECT_TRUE(isUnicodeEqualsCStr(sys_path0, abs_realpath));

  PyMem_RawFree(arg1);
  std::free(abs_realpath);
}

}  // namespace testing
}  // namespace py
