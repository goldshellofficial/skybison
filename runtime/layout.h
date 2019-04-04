#pragma once

#include "objects.h"
#include "utils.h"

namespace python {

// AttributeInfo packs attribute metadata into a SmallInt.
class AttributeInfo {
 public:
  explicit AttributeInfo(RawObject value) {
    DCHECK(value.isSmallInt(), "expected small integer");
    value_ = value.raw();
  }

  AttributeInfo() : value_(RawSmallInt::kTag) {}

  AttributeInfo(word offset, word flags) : value_(RawSmallInt::kTag) {
    DCHECK(isValidOffset(offset), "offset %ld too large (max is %ld)", offset,
           kMaxOffset);
    value_ |= (offset << kOffsetOffset);
    value_ |= (flags << kFlagsOffset);
  }

  // Getters and setters.

  // Retrieve the offset at which the attribute is stored.
  //
  // Check the kInObject flag to determine whether to retrieve the attribute
  // from the instance directly or from the overflow attributes.
  //
  // NB: For in-object attributes, this is the offset, in bytes, from the start
  // of the instance. For overflow attributes, this is the index into the
  // overflow array.
  word offset();
  static bool isValidOffset(word offset) { return offset <= kMaxOffset; }

  enum Flag {
    kNone = 0,

    // When set, this indicates that the attribute is stored directly on the
    // instance. When unset, this indicates that the attribute is stored in
    // the overflow array attached to the instance.
    kInObject = 1,

    // Only applies to in-object attributes. When set, it indicates that the
    // attribute has been deleted.
    kDeleted = 2,

    // Attribute lives at a fixed offset in the layout.
    kFixedOffset = 4,

    // Attribute is read-only for managed code.
    kReadOnly = 8,
  };

  word flags();
  bool testFlag(Flag flag);

  bool isInObject() { return testFlag(Flag::kInObject); }

  bool isOverflow() { return !testFlag(Flag::kInObject); }

  bool isDeleted() { return testFlag(Flag::kDeleted); }

  bool isFixedOffset() { return testFlag(Flag::kFixedOffset); }

  bool isReadOnly() { return testFlag(Flag::kReadOnly); }

  // Casting.
  RawSmallInt asSmallInt();

  // Tags.
  static const int kOffsetSize = 30;
  static const int kOffsetOffset = RawSmallInt::kTagSize;
  static const uword kOffsetMask = (1 << kOffsetSize) - 1;

  static const int kFlagsSize = 33;
  static const int kFlagsOffset = kOffsetOffset + kOffsetSize;
  static const uword kFlagsMask = (1UL << kFlagsSize) - 1;

  static_assert(
      RawSmallInt::kTagSize + kOffsetSize + kFlagsSize == kBitsPerPointer,
      "Number of bits used by AttributeInfo must fit in a RawSmallInt");

  // Constants
  static const word kMaxOffset = (1L << (kOffsetSize + 1)) - 1;

 private:
  uword value_;
};

inline word AttributeInfo::offset() {
  return (value_ >> kOffsetOffset) & kOffsetMask;
}

inline word AttributeInfo::flags() {
  return (value_ >> kFlagsOffset) & kFlagsMask;
}

inline bool AttributeInfo::testFlag(Flag flag) {
  return value_ & (static_cast<uword>(flag) << kFlagsOffset);
}

inline RawSmallInt AttributeInfo::asSmallInt() {
  return RawSmallInt::cast(RawObject{value_});
}

}  // namespace python
