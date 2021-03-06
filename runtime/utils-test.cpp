// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "utils.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "gtest/gtest.h"

#include "modules.h"
#include "test-utils.h"

namespace py {
namespace testing {

using UtilsTest = RuntimeFixture;
using UtilsDeathTest = RuntimeFixture;

TEST(UtilsTestNoFixture, MemoryFindWithEmptyHaystackReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFind(haystack, 0, needle, 2), -1);
}

TEST(UtilsTestNoFixture, MemoryFindWithZeroLengthNeedleReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFind(haystack, 5, needle, 0), -1);
}

TEST(UtilsTestNoFixture,
     MemoryFindWithHaystackLengthLessThanNeedleLengthReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFind(haystack, 1, needle, 2), -1);
}

TEST(UtilsTestNoFixture, MemoryFindWithCharNeedleInHaystackReturnsLocation) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFind(haystack, 5, needle, 1), 1);
}

TEST(UtilsTestNoFixture,
     MemoryFindWithCharNeedleNotInHaystackReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "q";
  EXPECT_EQ(Utils::memoryFind(haystack, 5, needle, 1), -1);
}

TEST(UtilsTestNoFixture, MemoryFindWithNeedleInHaystackReturnsLocation) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFind(haystack, 5, needle, 2), 1);
}

TEST(UtilsTestNoFixture,
     MemoryFindWithNeedleInHaystackReturnsFirstLocationFromLeft) {
  byte haystack[] = "hello hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFind(haystack, 11, needle, 2), 1);
}

TEST(UtilsTestNoFixture, MemoryFindWithNeedleNotInHaystackReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "qo";
  EXPECT_EQ(Utils::memoryFind(haystack, 5, needle, 2), -1);
}

TEST(UtilsTestNoFixture, MemoryFindCharWithEmptyHaystackReturnsNegativeOne) {
  byte haystack[] = "hello";
  int needle = 'h';
  EXPECT_EQ(Utils::memoryFindChar(haystack, 0, needle), -1);
}

TEST(UtilsTestNoFixture, MemoryFindCharWithNeedleInHaystackReturnsLocation) {
  byte haystack[] = "hello";
  int needle = 'h';
  EXPECT_EQ(Utils::memoryFindChar(haystack, 5, needle), 0);
}

TEST(UtilsTestNoFixture,
     MemoryFindCharWithNeedleInHaystackReturnsLeftmostLocation) {
  byte haystack[] = "helloh";
  int needle = 'h';
  EXPECT_EQ(Utils::memoryFindChar(haystack, 6, needle), 0);
}

TEST(UtilsTestNoFixture,
     MemoryFindCharReverseWithEmptyHaystackReturnsNegativeOne) {
  byte haystack[] = "hello";
  int needle = 'h';
  EXPECT_EQ(Utils::memoryFindCharReverse(haystack, 0, needle), -1);
}

TEST(UtilsTestNoFixture,
     MemoryFindCharReverseWithNeedleInHaystackReturnsLocation) {
  byte haystack[] = "hello";
  int needle = 'h';
  EXPECT_EQ(Utils::memoryFindCharReverse(haystack, 5, needle), 0);
}

TEST(UtilsTestNoFixture,
     MemoryFindCharReverseWithNeedleInHaystackReturnsRightmostLocation) {
  byte haystack[] = "helloh";
  int needle = 'h';
  EXPECT_EQ(Utils::memoryFindCharReverse(haystack, 6, needle), 5);
}

TEST(UtilsTestNoFixture, MemoryFindReverseWithEmptyHaystackReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFindReverse(haystack, 0, needle, 2), -1);
}

TEST(UtilsTestNoFixture, MemoryFindReverseWithEmptyNeedleReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFindReverse(haystack, 5, needle, 0), -1);
}

TEST(UtilsTestNoFixture,
     MemoryFindReverseWithHaystackSmallerThanNeedleReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFindReverse(haystack, 1, needle, 2), -1);
}

TEST(UtilsTestNoFixture,
     MemoryFindReverseWithCharNeedleInHaystackReturnsLocation) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFindReverse(haystack, 5, needle, 1), 1);
}

TEST(UtilsTestNoFixture,
     MemoryFindReverseWithCharNeedleNotInHaystackReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "q";
  EXPECT_EQ(Utils::memoryFindReverse(haystack, 5, needle, 1), -1);
}

TEST(UtilsTestNoFixture, MemoryFindReverseWithNeedleInHaystackReturnsLocation) {
  byte haystack[] = "hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFindReverse(haystack, 5, needle, 2), 1);
}

TEST(UtilsTestNoFixture,
     MemoryFindReverseWithNeedleInHaystackReturnsFirstLocationFromRight) {
  byte haystack[] = "hello hello";
  byte needle[] = "el";
  EXPECT_EQ(Utils::memoryFindReverse(haystack, 11, needle, 2), 7);
}

TEST(UtilsTestNoFixture,
     MemoryFindReverseWithNeedleNotInHaystackReturnsNegativeOne) {
  byte haystack[] = "hello";
  byte needle[] = "qo";
  EXPECT_EQ(Utils::memoryFindReverse(haystack, 5, needle, 2), -1);
}

TEST(UtilsTestNoFixture, RotateLeft) {
  EXPECT_EQ(Utils::rotateLeft(1ULL, 0), 0x0000000000000001ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 1), 0x0000000000000002ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 2), 0x0000000000000004ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 3), 0x0000000000000008ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 4), 0x0000000000000010ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 5), 0x0000000000000020ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 6), 0x0000000000000040ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 7), 0x0000000000000080ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 8), 0x0000000000000100ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 9), 0x0000000000000200ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 10), 0x0000000000000400ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 11), 0x0000000000000800ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 12), 0x0000000000001000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 13), 0x0000000000002000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 14), 0x0000000000004000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 15), 0x0000000000008000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 16), 0x0000000000010000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 17), 0x0000000000020000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 18), 0x0000000000040000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 19), 0x0000000000080000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 20), 0x0000000000100000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 21), 0x0000000000200000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 22), 0x0000000000400000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 23), 0x0000000000800000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 24), 0x0000000001000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 25), 0x0000000002000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 26), 0x0000000004000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 27), 0x0000000008000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 28), 0x0000000010000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 29), 0x0000000020000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 30), 0x0000000040000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 31), 0x0000000080000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 32), 0x0000000100000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 33), 0x0000000200000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 34), 0x0000000400000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 35), 0x0000000800000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 36), 0x0000001000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 37), 0x0000002000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 38), 0x0000004000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 39), 0x0000008000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 40), 0x0000010000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 41), 0x0000020000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 42), 0x0000040000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 43), 0x0000080000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 44), 0x0000100000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 45), 0x0000200000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 46), 0x0000400000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 47), 0x0000800000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 48), 0x0001000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 49), 0x0002000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 50), 0x0004000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 51), 0x0008000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 52), 0x0010000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 53), 0x0020000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 54), 0x0040000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 55), 0x0080000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 56), 0x0100000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 57), 0x0200000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 58), 0x0400000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 59), 0x0800000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 60), 0x1000000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 61), 0x2000000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 62), 0x4000000000000000ULL);
  EXPECT_EQ(Utils::rotateLeft(1ULL, 63), 0x8000000000000000ULL);
}

ALIGN_16 RawObject printDebugInfoAndAbortTest(Thread* thread, Arguments) {
  // Produce a pending exception with stacktrace!
  HandleScope scope(thread);
  Object call_raising(&scope, mainModuleAt(thread->runtime(), "call_raising"));
  EXPECT_TRUE(Interpreter::call0(thread, call_raising).isErrorException());

  Utils::printDebugInfoAndAbort();
}

TEST_F(UtilsDeathTest, PrintDebugInfoAndAbortPrintsTraceback) {
  addBuiltin("test_print_debug_info_and_abort", printDebugInfoAndAbortTest,
             View<const char*>(nullptr, 0), /*code_flags=*/0);
  ASSERT_DEATH(static_cast<void>(runFromCStr(runtime_, R"(
def raising():
  raise UserWarning("Hello")

def call_raising():
  raising()

def foo():
  test_print_debug_info_and_abort()

def bar():
  foo()

bar()
)")),
               R"(Stack \(most recent call first\):
  File "", line \?\?\? in test_print_debug_info_and_abort
  File "<test string>", line 9 in foo
  File "<test string>", line 12 in bar
  File "<test string>", line 14 in <module>
Pending exception
  Type          : <type "UserWarning">
  Value         : <"UserWarning" object>
  Exception Args: \("Hello",\)
  Traceback     : <"traceback" object>
Traceback \(most recent call last\):
  File "<test string>", line 6, in call_raising
  File "<test string>", line 3, in raising
)");
}

TEST_F(UtilsTest, RoundUpDiv) {
  EXPECT_EQ(Utils::roundUpDiv(17, 3), 6);
  EXPECT_EQ(Utils::roundUpDiv(17, 2), 9);
}

TEST(UtilsTestNoFixture, FailedDcheckWithNoThread) {
  EXPECT_EXIT(CHECK(false, "something went wrong!"),
              ::testing::KilledBySignal(SIGABRT), "something went wrong!");
}

}  // namespace testing
}  // namespace py
