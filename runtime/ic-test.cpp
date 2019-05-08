#include "gtest/gtest.h"

#include "ic.h"
#include "test-utils.h"

namespace python {

using namespace testing;

TEST(IcTest, IcPrepareBytecodeRewritesLoadAttrOperations) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  Object name(&scope, Str::empty());
  Code code(&scope, runtime.newEmptyCode(name));
  byte bytecode[] = {
      NOP,          99,        EXTENDED_ARG, 0xca, LOAD_ATTR,    0xfe,
      NOP,          LOAD_ATTR, EXTENDED_ARG, 1,    EXTENDED_ARG, 2,
      EXTENDED_ARG, 3,         STORE_ATTR,   4,    LOAD_ATTR,    77,
  };
  code.setCode(runtime.newBytesWithAll(bytecode));
  Object none(&scope, NoneType::object());
  Dict globals(&scope, runtime.newDict());
  Dict builtins(&scope, runtime.newDict());
  Function function(
      &scope, Interpreter::makeFunction(thread, name, code, none, none, none,
                                        none, globals, builtins));

  icRewriteBytecode(thread, function);

  byte expected[] = {
      NOP,          99,        EXTENDED_ARG, 0, LOAD_ATTR,    0,
      NOP,          LOAD_ATTR, EXTENDED_ARG, 0, EXTENDED_ARG, 0,
      EXTENDED_ARG, 0,         STORE_ATTR,   1, LOAD_ATTR,    2,
  };
  Object rewritten_bytecode(&scope, function.rewrittenBytecode());
  EXPECT_TRUE(isBytesEqualsBytes(rewritten_bytecode, expected));

  ASSERT_TRUE(function.caches().isTuple());
  Tuple caches(&scope, function.caches());
  EXPECT_EQ(caches.length(), 3 * kIcPointersPerCache);
  for (word i = 0, length = caches.length(); i < length; i++) {
    EXPECT_TRUE(caches.at(i).isNoneType()) << "index " << i;
  }

  EXPECT_EQ(icOriginalArg(function, 0), 0xcafe);
  EXPECT_EQ(icOriginalArg(function, 1), 0x01020304);
  EXPECT_EQ(icOriginalArg(function, 2), 77);
}

static RawObject layoutIdAsSmallInt(LayoutId id) {
  return SmallInt::fromWord(static_cast<word>(id));
}

TEST(IcTest, IcLookupReturnsFirstCachedValue) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);

  Tuple caches(&scope, runtime.newTuple(1 * kIcPointersPerCache));
  caches.atPut(kIcEntryKeyOffset, layoutIdAsSmallInt(LayoutId::kSmallInt));
  caches.atPut(kIcEntryValueOffset, runtime.newInt(44));
  EXPECT_TRUE(isIntEqualsWord(icLookup(caches, 0, LayoutId::kSmallInt), 44));
}

TEST(IcTest, IcLookupReturnsFourthCachedValue) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);

  Tuple caches(&scope, runtime.newTuple(2 * kIcPointersPerCache));
  caches.atPut(kIcEntryKeyOffset, layoutIdAsSmallInt(LayoutId::kSmallInt));
  word cache_offset = kIcPointersPerCache;
  caches.atPut(cache_offset + 0 * kIcPointersPerEntry + kIcEntryKeyOffset,
               layoutIdAsSmallInt(LayoutId::kSmallStr));
  caches.atPut(cache_offset + 1 * kIcPointersPerEntry + kIcEntryKeyOffset,
               layoutIdAsSmallInt(LayoutId::kStopIteration));
  caches.atPut(cache_offset + 2 * kIcPointersPerEntry + kIcEntryKeyOffset,
               layoutIdAsSmallInt(LayoutId::kLargeStr));
  caches.atPut(cache_offset + 3 * kIcPointersPerEntry + kIcEntryKeyOffset,
               layoutIdAsSmallInt(LayoutId::kSmallInt));
  caches.atPut(cache_offset + 3 * kIcPointersPerEntry + kIcEntryValueOffset,
               runtime.newInt(7));
  EXPECT_TRUE(isIntEqualsWord(icLookup(caches, 1, LayoutId::kSmallInt), 7));
}

TEST(IcTest, IcLookupWithoutMatchReturnsErrorNotFound) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);

  Tuple caches(&scope, runtime.newTuple(2 * kIcPointersPerCache));
  EXPECT_TRUE(icLookup(caches, 1, LayoutId::kSmallInt).isErrorNotFound());
}

TEST(IcTest, IcFindReturnsFreeEntryIndex) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);

  Tuple caches(&scope, runtime.newTuple(1 * kIcPointersPerCache));
  EXPECT_EQ(icFind(caches, 0, LayoutId::kSmallStr), 0);
}

TEST(IcTest, IcFindReturnsExistingEntryIndex) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);

  Tuple caches(&scope, runtime.newTuple(2 * kIcPointersPerCache));
  word cache_offset = kIcPointersPerCache;
  caches.atPut(cache_offset + 0 * kIcPointersPerEntry + kIcEntryKeyOffset,
               layoutIdAsSmallInt(LayoutId::kSmallInt));
  caches.atPut(cache_offset + 1 * kIcPointersPerEntry + kIcEntryKeyOffset,
               layoutIdAsSmallInt(LayoutId::kSmallBytes));
  caches.atPut(cache_offset + 2 * kIcPointersPerEntry + kIcEntryKeyOffset,
               layoutIdAsSmallInt(LayoutId::kNoneType));
  caches.atPut(cache_offset + 3 * kIcPointersPerEntry + kIcEntryKeyOffset,
               layoutIdAsSmallInt(LayoutId::kSmallStr));
  EXPECT_EQ(icFind(caches, 1, LayoutId::kSmallStr),
            1 * kIcPointersPerCache + 3 * kIcPointersPerEntry);
}

TEST(IcTest, IcFindReturnsMinusOneOnFullCache) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);

  Tuple caches(&scope, runtime.newTuple(1 * kIcPointersPerCache));
  for (word i = 0; i < kIcEntriesPerCache; i++) {
    caches.atPut(i * kIcPointersPerEntry + kIcEntryKeyOffset,
                 SmallInt::fromWord(1000 + i));
  }
  EXPECT_EQ(icFind(caches, 0, LayoutId::kLargeInt), -1);
}

TEST(IcTest, IcUpdateEntrySetsValues) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);

  Tuple caches(&scope, runtime.newTuple(2 * kIcPointersPerCache));
  word offset = 1 * kIcPointersPerEntry;
  icUpdate(caches, offset, LayoutId::kUserWarning, Str::empty());
  EXPECT_TRUE(isIntEqualsWord(caches.at(offset + kIcEntryKeyOffset),
                              static_cast<word>(LayoutId::kUserWarning)));
  EXPECT_TRUE(isStrEqualsCStr(caches.at(offset + kIcEntryValueOffset), ""));
}

}  // namespace python
