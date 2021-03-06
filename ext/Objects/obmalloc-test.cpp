// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Python.h"
#include "gtest/gtest.h"

#include "capi-fixture.h"
#include "capi-testing.h"

namespace py {
namespace testing {

using ObmallocExtensionApiTest = ExtensionApi;

TEST_F(ObmallocExtensionApiTest, PyObjectDebugMallocStatsReturnsZeroPyro) {
  EXPECT_EQ(_PyObject_DebugMallocStats(nullptr), 0);
}

TEST_F(ObmallocExtensionApiTest, PyMemRawStrdupDuplicatesStr) {
  const char* str = "hello, world";
  char* dup = _PyMem_RawStrdup(str);
  EXPECT_NE(dup, str);
  EXPECT_STREQ(dup, str);
  PyMem_RawFree(dup);
}

TEST_F(ObmallocExtensionApiTest, PyMemStrdupDuplicatesStr) {
  const char* str = "hello, world";
  char* dup = _PyMem_Strdup(str);
  EXPECT_NE(dup, str);
  EXPECT_STREQ(dup, str);
  PyMem_Free(dup);
}

TEST_F(ObmallocExtensionApiTest, PyMemMemResizeAssignsToPointer) {
  void* ptr = nullptr;
  PyMem_Resize(ptr, int, 128);
  EXPECT_NE(ptr, nullptr);
  PyMem_Free(ptr);
}

TEST_F(ObmallocExtensionApiTest, PyMemMemResizeMovesContents) {
  char* ptr = PyMem_New(char, 1);
  ASSERT_NE(ptr, nullptr);
  *ptr = 98;

  // Allocate the next word and resize to a much larger memory
  void* intervening_allocation = PyMem_New(char, 1);
  PyMem_Resize(ptr, char, 65536);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 98);
  ptr[65535] = 87;
  PyMem_FREE(intervening_allocation);

  PyMem_RESIZE(ptr, char, 1048576);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 98);
  EXPECT_EQ(ptr[65535], 87);
  PyMem_FREE(ptr);
}

TEST_F(ObmallocExtensionApiTest, PyMemMallocAllocatesMemory) {
  void* ptr = PyObject_Malloc(1);
  EXPECT_NE(ptr, nullptr);
  PyObject_Free(ptr);
}

TEST_F(ObmallocExtensionApiTest, PyMemCallocAllocatesMemory) {
  void* ptr = PyObject_Calloc(1, 1);
  EXPECT_NE(ptr, nullptr);
  PyObject_Free(ptr);
}

TEST_F(ObmallocExtensionApiTest, PyMemReallocAllocatesMemory) {
  auto* ptr = reinterpret_cast<char*>(PyObject_Malloc(1));
  ASSERT_NE(ptr, nullptr);
  *ptr = 98;
  ptr = reinterpret_cast<char*>(PyObject_Realloc(ptr, 2));
  ASSERT_NE(ptr, nullptr);
  ptr[1] = 87;

  EXPECT_EQ(*ptr, 98);
  EXPECT_EQ(ptr[1], 87);
  PyObject_Free(ptr);
}

TEST_F(ObmallocExtensionApiTest, PyMemReallocOnlyRetracksPyObjects) {
  auto* ptr = reinterpret_cast<char*>(PyObject_Malloc(1));
  ASSERT_NE(ptr, nullptr);
  *ptr = 98;

  collectGarbage();

  ptr = reinterpret_cast<char*>(PyObject_Realloc(ptr, 2));
  ASSERT_NE(ptr, nullptr);
  ptr[1] = 87;

  EXPECT_EQ(*ptr, 98);
  EXPECT_EQ(ptr[1], 87);
  PyObject_Free(ptr);
}

TEST_F(ObmallocExtensionApiTest, PyMemNewAllocatesAndPyMemDelFreesMemory) {
  struct FooBar {
    char x[7];
  };
  void* memory = PyMem_New(FooBar, 3);
  ASSERT_NE(memory, nullptr);
  memset(memory, 8, 3 * sizeof(FooBar));
  PyMem_Del(memory);
}

TEST_F(ObmallocExtensionApiTest, PyMemNEWAllocatesAndPyMemDELFreesMemory) {
  struct FooBar {
    char x[7];
  };
  void* memory = PyMem_NEW(FooBar, 3);
  ASSERT_NE(memory, nullptr);
  memset(memory, 8, 3 * sizeof(FooBar));
  PyMem_DEL(memory);
}

}  // namespace testing
}  // namespace py
