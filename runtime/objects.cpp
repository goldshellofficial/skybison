#include "objects.h"

#include "bytes-builtins.h"
#include "frame.h"
#include "runtime.h"
#include "thread.h"

#include <cstring>

namespace py {

// RawSmallBytes

RawObject RawSmallBytes::becomeStr() const {
  return RawObject{raw() ^ kSmallBytesTag ^ kSmallStrTag};
}

word RawSmallBytes::findByte(byte value, word start, word length) const {
  DCHECK_BOUND(start, this->length());
  DCHECK_BOUND(start + length, this->length());
  for (word i = 0; i < length; i++) {
    if (byteAt(start + i) == value) return start + i;
  }
  return -1;
}

RawSmallBytes RawSmallBytes::fromBytes(View<byte> data) {
  word length = data.length();
  DCHECK_BOUND(length, kMaxLength);
  uword result = 0;
  for (word i = length - 1; i >= 0; i--) {
    result = (result << kBitsPerByte) | data.get(i);
  }
  return RawSmallBytes(result << kBitsPerByte | length << kImmediateTagBits |
                       kSmallBytesTag);
}

bool RawSmallBytes::isASCII() const {
  uword block = raw() >> kBitsPerByte;
  uword non_ascii_mask = (~uword{0} / 0xFF) << (kBitsPerByte - 1);
  return (block & non_ascii_mask) == 0;
}

// RawSmallStr

RawObject RawSmallStr::becomeBytes() const {
  return RawObject{raw() ^ kSmallStrTag ^ kSmallBytesTag};
}

word RawSmallStr::compare(RawObject that) const {
  word length = charLength();
  for (word i = 0; i < length; i++) {
    word result = charAt(i) - RawLargeStr::cast(that).charAt(i);
    if (result != 0) return result;
  }
  return -1;
}

RawSmallStr RawSmallStr::fromCodePoint(int32_t code_point) {
  DCHECK_BOUND(code_point, kMaxUnicode);
  uword cp = static_cast<uword>(code_point);
  // 0xxxxxxx
  if (cp <= kMaxASCII) {  // 01111111
    return RawSmallStr(cp << 8 | (1 << kImmediateTagBits) | kSmallStrTag);
  }
  uword result = cp & 0x3F;  // 00111111
  cp >>= 6;
  result <<= kBitsPerByte;
  // 110xxxxx 10xxxxxx
  if (cp <= 0x1F) {  // 00011111
    result |= cp;
    result |= 0x80C0;  // 10xxxxxx 110xxxxx
    result <<= kBitsPerByte;
    return RawSmallStr(result | (2 << kImmediateTagBits) | kSmallStrTag);
  }

  result |= cp & 0x3F;  // 00111111
  cp >>= 6;
  result <<= kBitsPerByte;
  // 1110xxxx 10xxxxxx 10xxxxxx
  if (cp <= 0xF) {  // 00001111
    result |= cp;
    result |= 0x8080E0;  // 10xxxxxx 10xxxxxx 1110xxxx
    result <<= kBitsPerByte;
    return RawSmallStr(result | (3 << kImmediateTagBits) | kSmallStrTag);
  }
  result |= cp & 0x3F;  // 00111111
  cp >>= 6;
  result <<= kBitsPerByte;
  // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
  result |= cp;
  result |= 0x808080F0;  // 10xxxxxx 10xxxxxx 10xxxxxx 11110xxx
  result <<= kBitsPerByte;
  return RawSmallStr(result | (4 << kImmediateTagBits) | kSmallStrTag);
}

RawSmallStr RawSmallStr::fromCStr(const char* value) {
  word len = std::strlen(value);
  return fromBytes(View<byte>(reinterpret_cast<const byte*>(value), len));
}

RawSmallStr RawSmallStr::fromBytes(View<byte> data) {
  word length = data.length();
  DCHECK_BOUND(length, kMaxLength);
  uword result = 0;
  for (word i = length - 1; i >= 0; i--) {
    result = (result << kBitsPerByte) | data.get(i);
  }
  return RawSmallStr(result << kBitsPerByte | length << kImmediateTagBits |
                     kSmallStrTag);
}

bool RawSmallStr::isASCII() const {
  uword block = raw() >> kBitsPerByte;
  uword non_ascii_mask = (~uword{0} / 0xFF) << (kBitsPerByte - 1);
  return (block & non_ascii_mask) == 0;
}

char* RawSmallStr::toCStr() const {
  word length = charLength();
  byte* result = static_cast<byte*>(std::malloc(length + 1));
  CHECK(result != nullptr, "out of memory");
  copyTo(result, length);
  result[length] = '\0';
  return reinterpret_cast<char*>(result);
}

word RawSmallStr::codePointLength() const {
  uword block = raw() >> kBitsPerByte;
  uword mask_0 = ~uword{0} / 0xFF;  // 0x010101...
  uword mask_7 = mask_0 << 7;       // 0x808080...
  block = ((block & mask_7) >> 7) & ((~block) >> 6);
  // TODO(cshapiro): evaluate using popcount instead of multiplication
  word num_trailing = (block * mask_0) >> ((kWordSize - 1) * kBitsPerByte);
  return charLength() - num_trailing;
}

// RawSmallInt

const word RawSmallInt::kMinValue;
const word RawSmallInt::kMaxValue;

// RawByteArray

word RawByteArray::compare(RawBytes that, word that_len) {
  DCHECK_BOUND(that_len, that.length());
  word this_len = this->numItems();
  word len = Utils::minimum(this_len, that_len);
  for (word i = 0; i < len; i++) {
    word diff = this->byteAt(i) - that.byteAt(i);
    if (diff != 0) return diff;
  }
  return this_len - that_len;
}

void RawByteArray::downsize(word new_length) const {
  word original_length = numItems();
  DCHECK_BOUND(new_length, original_length);
  if (original_length == 0) return;
  byte* dst = reinterpret_cast<byte*>(RawMutableBytes::cast(bytes()).address());
  std::memset(dst + new_length, 0, original_length - new_length);
  setNumItems(new_length);
}

void RawByteArray::replaceFromWith(word dst_start, RawByteArray src,
                                   word count) const {
  DCHECK_BOUND(dst_start + count, numItems());
  MutableBytes::cast(bytes()).replaceFromWith(dst_start,
                                              Bytes::cast(src.bytes()), count);
}

void RawByteArray::replaceFromWithStartAt(word dst_start, RawByteArray src,
                                          word count, word src_start) const {
  DCHECK_BOUND(dst_start + count, numItems());
  DCHECK_BOUND(src_start + count, src.numItems());
  MutableBytes::cast(bytes()).replaceFromWithStartAt(
      dst_start, Bytes::cast(src.bytes()), count, src_start);
}

// RawBytes

word RawBytes::compare(RawBytes that) const {
  word this_len = this->length();
  word that_len = that.length();
  word len = Utils::minimum(this_len, that_len);
  for (word i = 0; i < len; i++) {
    word diff = this->byteAt(i) - that.byteAt(i);
    if (diff != 0) return diff;
  }
  return this_len - that_len;
}

// RawCode

word RawCode::offsetToLineNum(word offset) const {
  // See https://github.com/python/cpython/blob/master/Objects/lnotab_notes.txt
  // for details about the line number table format
  RawBytes table = Bytes::cast(lnotab());
  word line = firstlineno();
  word cur_offset = 0;
  for (word i = 0; i < table.length(); i += 2) {
    cur_offset += table.byteAt(i);
    if (cur_offset > offset) {
      break;
    }
    line += static_cast<int8_t>(table.byteAt(i + 1));
  }
  return line;
}

// RawLargeBytes

RawObject RawLargeBytes::becomeStr() const {
  DCHECK(bytesIsValidStr(RawBytes::cast(*this)), "must contain valid utf-8");
  setHeader(header().withLayoutId(LayoutId::kLargeStr));
  return *this;
}

word RawLargeBytes::findByte(byte value, word start, word length) const {
  DCHECK_BOUND(start, this->length());
  DCHECK_BOUND(start + length, this->length());
  word result = Utils::memoryFindChar(
      reinterpret_cast<byte*>(address() + start), value, length);
  if (result != -1) result += start;
  return result;
}

bool RawLargeBytes::isASCII() const {
  // Depends on invariants specified in RawLargeStr::codePointLength
  word length = this->length();
  word size_in_words = (length + kWordSize - 1) >> kWordSizeLog2;
  const uword* data = reinterpret_cast<const uword*>(address());
  uword non_ascii_mask = (~uword{0} / 0xFF) << (kBitsPerByte - 1);
  for (word i = 0; i < size_in_words; i++) {
    // Read an entire word of code units.
    uword block = data[i];
    if ((block & non_ascii_mask) != 0) return false;
  }
  return true;
}

// RawLargeStr

word RawLargeStr::compare(RawObject that) const {
  word this_length = charLength();
  word that_length = RawLargeStr::cast(that).charLength();
  word length = Utils::minimum(this_length, that_length);
  auto s1 = reinterpret_cast<void*>(address());
  auto s2 = reinterpret_cast<void*>(RawLargeStr::cast(that).address());
  word result = std::memcmp(s1, s2, length);
  return result != 0 ? result : this_length - that_length;
}

bool RawLargeStr::equals(RawObject that) const {
  if (!that.isLargeStr()) {
    return false;
  }
  auto that_str = RawLargeStr::cast(that);
  if (length() != that_str.length()) {
    return false;
  }
  auto s1 = reinterpret_cast<void*>(address());
  auto s2 = reinterpret_cast<void*>(that_str.address());
  return std::memcmp(s1, s2, length()) == 0;
}

bool RawLargeStr::equalsBytes(View<byte> bytes) const {
  word length = this->length();
  if (bytes.length() != length) {
    return false;
  }
  const void* chars = reinterpret_cast<const void*>(address());
  return std::memcmp(chars, bytes.data(), length) == 0;
}

void RawLargeStr::copyTo(byte* dst, word length) const {
  DCHECK_BOUND(length, this->length());
  std::memcpy(dst, reinterpret_cast<const byte*>(address()), length);
}

void RawLargeStr::copyToStartAt(byte* dst, word char_length,
                                word char_start) const {
  DCHECK_BOUND(char_start, charLength());
  DCHECK_BOUND(char_start + char_length, charLength());
  std::memcpy(dst, reinterpret_cast<const byte*>(address() + char_start),
              char_length);
}

char* RawLargeStr::toCStr() const {
  word length = this->length();
  byte* result = static_cast<byte*>(std::malloc(length + 1));
  CHECK(result != nullptr, "out of memory");
  copyTo(result, length);
  result[length] = '\0';
  return reinterpret_cast<char*>(result);
}

word RawLargeStr::codePointLength() const {
  // This is a vectorized loop for processing code units in groups the size of a
  // machine word.  The garbage collector ensures the following invariants that
  // simplify the algorithm, eliminating the need for a scalar pre-loop or a
  // scalar-post loop:
  //
  //   1) The base address of instance data is always word aligned
  //   2) The allocation sizes are always rounded-up to the next word
  //   3) Unused bytes at the end of an allocation are always zero
  //
  // This algorithm works by counting the number of UTF-8 trailing bytes found
  // in the string from the total number of byte in the string.  Because the
  // unused bytes at the end of a string are zero they are conveniently ignored
  // by the counting.
  word length = this->length();
  word size_in_words = (length + kWordSize - 1) >> kWordSizeLog2;
  word result = length;
  const uword* data = reinterpret_cast<const uword*>(address());
  uword mask_0 = ~uword{0} / 0xFF;  // 0x010101...
  uword mask_7 = mask_0 << 7;       // 0x808080...
  for (word i = 0; i < size_in_words; i++) {
    // Read an entire word of code units.
    uword block = data[i];
    // The bit pattern 0b10xxxxxx identifies a UTF-8 trailing byte.  For each
    // byte in a word, we isolate bit 6 and 7 and logically and the complement
    // of bit 6 with bit 7.  That leaves one set bit for each trailing byte in a
    // word.
    block = ((block & mask_7) >> 7) & ((~block) >> 6);
    // Count the number of bits leftover in the word.  That is equal to the
    // number of trailing bytes.
    // TODO(cshapiro): evaluate using popcount instead of multiplication
    word num_trailing = (block * mask_0) >> ((kWordSize - 1) * kBitsPerByte);
    // Finally, subtract the number of trailing bytes from the number of bytes
    // in the string leaving just the number of ASCII code points and UTF-8
    // leading bytes in the count.
    result -= num_trailing;
  }
  return result;
}

bool RawLargeStr::isASCII() const {
  // Depends on invariants specified in RawLargeStr::codePointLength
  word length = this->length();
  word size_in_words = (length + kWordSize - 1) >> kWordSizeLog2;
  const uword* data = reinterpret_cast<const uword*>(address());
  uword non_ascii_mask = (~uword{0} / 0xFF) << (kBitsPerByte - 1);
  for (word i = 0; i < size_in_words; i++) {
    // Read an entire word of code units.
    uword block = data[i];
    if ((block & non_ascii_mask) != 0) return false;
  }
  return true;
}

// RawList

void RawList::replaceFromWith(word start, RawList src, word count) const {
  DCHECK_BOUND(start + count, numItems());
  MutableTuple::cast(items()).replaceFromWith(start, Tuple::cast(src.items()),
                                              count);
}

void RawList::replaceFromWithStartAt(word start, RawList src, word count,
                                     word src_start) const {
  DCHECK_BOUND(start + count, numItems());
  DCHECK_BOUND(src_start + count, src.numItems());
  MutableTuple::cast(items()).replaceFromWithStartAt(
      start, Tuple::cast(src.items()), count, src_start);
}

// RawInt

word RawInt::compare(RawInt that) const {
  if (this->isSmallInt() && that.isSmallInt()) {
    return this->asWord() - that.asWord();
  }
  // compare with large ints always returns -1, 0, or 1
  bool is_negative = this->isNegative();
  if (is_negative != that.isNegative()) {
    return is_negative ? -1 : 1;
  }

  word left_digits = this->numDigits();
  word right_digits = that.numDigits();

  if (left_digits > right_digits) {
    return is_negative ? -1 : 1;
  }
  if (left_digits < right_digits) {
    return is_negative ? 1 : -1;
  }
  for (word i = left_digits - 1; i >= 0; i--) {
    uword left_digit = this->digitAt(i);
    uword right_digit = that.digitAt(i);
    if (left_digit > right_digit) {
      return 1;
    }
    if (left_digit < right_digit) {
      return -1;
    }
  }
  return 0;
}

word RawInt::copyTo(byte* dst, word max_length) const {
  if (isLargeInt()) {
    return RawLargeInt::cast(*this).copyTo(dst, max_length);
  }

  DCHECK(isSmallInt() || isBool(), "not an integer");
  uword val = isSmallInt() ? RawSmallInt::cast(*this).value()
                           : RawBool::cast(*this).value();
  word memcpy_length = std::min(word{kWordSize}, max_length);
  std::memcpy(dst, &val, memcpy_length);
  return memcpy_length;
}

// RawLargeInt

bool RawLargeInt::isValid() const {
  word digits = numDigits();
  if (digits <= 0) {
    return false;
  }
  if (digits == 1) {
    // Enforce a canonical representation for all ints.
    return !RawSmallInt::isValid(digitAt(0));
  }

  word high_digit = digitAt(digits - 1);
  word next_digit = digitAt(digits - 2);

  // Redundant sign-extension for negative values.
  if (high_digit == -1 && next_digit < 0) {
    return false;
  }

  // Redundant zero-extension for positive values.
  if (high_digit == 0 && next_digit >= 0) {
    return false;
  }

  return true;
}

word RawLargeInt::bitLength() const {
  word num_digits = numDigits();
  word high_digit = digitAt(num_digits - 1);

  if (high_digit < 0) {
    // We're negative. Calculate what high_digit would be after negation.
    uword carry = [&] {
      for (word i = 0; i < num_digits - 1; i++) {
        if (digitAt(i) != 0) return 0;
      }
      return 1;
    }();
    high_digit = ~high_digit + carry;
  }
  return (num_digits - 1) * kBitsPerWord + Utils::highestBit(high_digit);
}

word RawLargeInt::copyTo(byte* dst, word copy_length) const {
  word length = numDigits() * kWordSize;
  auto digits = reinterpret_cast<uword*>(address() + kValueOffset);
  word memcpy_size = std::min(length, copy_length);
  std::memcpy(dst, digits, memcpy_size);
  return memcpy_size;
}

void RawLargeInt::copyFrom(RawBytes bytes, byte sign_extension) const {
  auto dst = reinterpret_cast<byte*>(address() + kValueOffset);
  word bytes_len = bytes.length();
  DCHECK(bytes_len <= numDigits() * kWordSize, "too many bytes");
  bytes.copyTo(dst, bytes_len);
  std::memset(dst + bytes_len, sign_extension,
              (numDigits() * kWordSize) - bytes_len);
}

// RawMutableBytes

void RawMutableBytes::replaceFromWith(word dst_start, RawBytes src,
                                      word count) const {
  DCHECK_BOUND(dst_start + count, length());
  byte* dst = reinterpret_cast<byte*>(address());
  src.copyTo(dst + dst_start, count);
}

void RawMutableBytes::replaceFromWithStartAt(word dst_start, RawBytes src,
                                             word count, word src_start) const {
  DCHECK_BOUND(dst_start + count, length());
  DCHECK_BOUND(src_start + count, src.length());
  src.copyToStartAt(reinterpret_cast<byte*>(address() + dst_start), count,
                    src_start);
}

void RawMutableBytes::replaceFromWithStr(word index, RawStr src,
                                         word char_length) const {
  DCHECK_BOUND(index + char_length, length());
  byte* dst = reinterpret_cast<byte*>(address());
  src.copyTo(dst + index, char_length);
}

void RawMutableBytes::replaceFromWithStrStartAt(word dst_start, RawStr src,
                                                word char_length,
                                                word src_start_char) const {
  DCHECK_BOUND(dst_start + char_length, length());
  byte* dst = reinterpret_cast<byte*>(address());
  src.copyToStartAt(dst + dst_start, char_length, src_start_char);
}

RawObject RawMutableBytes::becomeImmutable() const {
  word len = length();
  if (len <= SmallBytes::kMaxLength) {
    return SmallBytes::fromBytes({reinterpret_cast<byte*>(address()), len});
  }
  setHeader(header().withLayoutId(LayoutId::kLargeBytes));
  return *this;
}

RawObject RawMutableBytes::becomeStr() const {
  DCHECK(bytesIsValidStr(RawBytes::cast(*this)), "must contain valid utf-8");
  word len = length();
  if (len <= SmallStr::kMaxLength) {
    return SmallStr::fromBytes({reinterpret_cast<byte*>(address()), len});
  }
  setHeader(header().withLayoutId(LayoutId::kLargeStr));
  return *this;
}

// RawMutableTuple

void RawMutableTuple::fill(RawObject value) const {
  word len = length();
  if (value.isNoneType()) {
    initialize();
    return;
  }
  for (word i = 0; i < len; i++) {
    atPut(i, value);
  }
}

void RawMutableTuple::replaceFromWith(word dst_start, RawTuple src,
                                      word count) const {
  replaceFromWithStartAt(dst_start, src, count, 0);
}

void RawMutableTuple::replaceFromWithStartAt(word dst_start, RawTuple src,
                                             word count, word src_start) const {
  if (src != *this) {
    // No overlap
    for (word i = dst_start, j = src_start; count != 0; i++, j++, count--) {
      atPut(i, src.at(j));
    }
  } else if (src_start < dst_start) {
    // Overlap; copy backward
    for (word i = dst_start + count - 1, j = src_start + count - 1; count != 0;
         i--, j--, count--) {
      atPut(i, src.at(j));
    }
  } else if (src_start > dst_start) {
    // Overlap; copy forward
    for (word i = dst_start, j = src_start; count != 0; i++, j++, count--) {
      atPut(i, src.at(j));
    }
  }
}

// RawTuple

bool RawTuple::contains(RawObject object) const {
  word len = length();
  for (word i = 0; i < len; i++) {
    if (at(i) == object) {
      return true;
    }
  }
  return false;
}

// RawSlice

word RawSlice::length(word start, word stop, word step) {
  if (step < 0) {
    if (stop < start) {
      return (start - stop - 1) / (-step) + 1;
    }
  } else if (start < stop) {
    return (stop - start - 1) / step + 1;
  }
  return 0;
}

word RawSlice::adjustIndices(word length, word* start, word* stop, word step) {
  DCHECK(step != 0, "Step should be non zero");

  if (*start < 0) {
    *start += length;
    if (*start < 0) {
      *start = (step < 0) ? -1 : 0;
    }
  } else if (*start >= length) {
    *start = (step < 0) ? length - 1 : length;
  }

  if (*stop < 0) {
    *stop += length;
    if (*stop < 0) {
      *stop = (step < 0) ? -1 : 0;
    }
  } else if (*stop >= length) {
    *stop = (step < 0) ? length - 1 : length;
  }

  return RawSlice::length(*start, *stop, step);
}

void RawSlice::adjustSearchIndices(word* start, word* end, word length) {
  if (*start < 0) {
    *start = Utils::maximum(*start + length, word{0});
  }
  if (*end < 0) {
    *end = Utils::maximum(*end + length, word{0});
  } else if (*end > length) {
    *end = length;
  }
}

// RawStr

word RawStr::compareCStr(const char* c_str) const {
  word c_length = std::strlen(c_str);
  word length = Utils::minimum(charLength(), c_length);
  for (word i = 0; i < length; i++) {
    word diff = charAt(i) - static_cast<byte>(c_str[i]);
    if (diff != 0) {
      return (diff > 0) ? 1 : -1;
    }
  }
  word diff = charLength() - c_length;
  return (diff > 0) ? 1 : ((diff < 0) ? -1 : 0);
}

bool RawStr::equalsCStr(const char* c_str) const {
  const char* cp = c_str;
  const word len = charLength();
  for (word i = 0; i < len; i++, cp++) {
    byte ch = static_cast<byte>(*cp);
    if (ch == '\0' || ch != charAt(i)) {
      return false;
    }
  }
  return *cp == '\0';
}

template <typename T, typename F>
static inline int32_t decodeCodePoint(T src, F at, word src_length, word index,
                                      word* char_length) {
  DCHECK_INDEX(index, src_length);
  byte b0 = (src->*at)(index);
  if (b0 <= kMaxASCII) {
    *char_length = 1;
    return b0;
  }
  DCHECK_INDEX(index + 1, src_length);
  byte b1 = (src->*at)(index + 1) & byte{0x3F};
  // 0b110xxxxx begins a sequence with one continuation byte.
  if (b0 < 0xE0) {
    DCHECK(b0 >= 0xC, "unexpected continuation byte");
    *char_length = 2;
    return ((b0 & 0x1F) << 6) | b1;
  }
  DCHECK_INDEX(index + 2, src_length);
  byte b2 = (src->*at)(index + 2) & byte{0x3F};
  // 0b1110xxxx starts a sequence with two continuation bytes.
  if (b0 < 0xF0) {
    *char_length = 3;
    return ((b0 & 0xF) << 12) | (b1 << 6) | b2;
  }
  // 0b11110xxx starts a sequence with three continuation bytes.
  DCHECK((b0 & 0xF8) == 0xF0, "invalid code unit");
  DCHECK_INDEX(index + 2, src_length);
  byte b3 = (src->*at)(index + 3) & byte{0x3F};
  *char_length = 4;
  return ((b0 & 0x7) << 18) | (b1 << 12) | (b2 << 6) | b3;
}

int32_t RawStr::codePointAt(word index, word* char_length) const {
  return decodeCodePoint(this, &RawStr::charAt, charLength(), index,
                         char_length);
}

word RawStr::offsetByCodePoints(word index, word count) const {
  if (count >= 0) {
    word len = charLength();
    while (count-- && index < len) {
      byte ch = charAt(index);
      if (ch <= kMaxASCII) {
        index++;
      } else if ((ch & 0xE0) == 0xC0) {
        index += 2;
      } else if ((ch & 0xF0) == 0xE0) {
        index += 3;
      } else {
        DCHECK((ch & 0xF8) == 0xF0, "invalid code unit");
        index += 4;
      }
    }
    return Utils::minimum(index, len);
  }
  while (count < 0) {
    index--;
    if (index < 0) return -1;
    if ((charAt(index) & 0xC0) != 0x80) count++;
  }
  return index;
}

// RawStrArray

int32_t RawStrArray::codePointAt(word index, word* char_length) const {
  RawMutableBytes buffer = RawMutableBytes::cast(items());
  return decodeCodePoint(&buffer, &RawMutableBytes::byteAt, numItems(), index,
                         char_length);
}

// Linked list helpers

static void enqueueReference(RawObject reference, RawObject* tail,
                             word link_offset) {
  DCHECK(Instance::cast(reference).instanceVariableAt(link_offset).isNoneType(),
         "Attempting to enqueue object that's already in queue");
  if (*tail == RawNoneType::object()) {
    Instance::cast(reference).instanceVariableAtPut(link_offset, reference);
  } else {
    RawObject head = Instance::cast(*tail).instanceVariableAt(link_offset);
    Instance::cast(*tail).instanceVariableAtPut(link_offset, reference);
    Instance::cast(reference).instanceVariableAtPut(link_offset, head);
  }
  *tail = reference;
}

static RawObject dequeueReference(RawObject* tail, word link_offset) {
  DCHECK(*tail != RawNoneType::object(), "empty queue");
  RawObject head = Instance::cast(*tail).instanceVariableAt(link_offset);
  if (head == *tail) {
    *tail = RawNoneType::object();
  } else {
    RawObject next = Instance::cast(head).instanceVariableAt(link_offset);
    Instance::cast(*tail).instanceVariableAtPut(link_offset, next);
  }
  Instance::cast(head).instanceVariableAtPut(link_offset,
                                             RawNoneType::object());
  return head;
}

// RawWeakRef

void RawWeakRef::enqueue(RawObject reference, RawObject* tail) {
  enqueueReference(reference, tail, RawWeakRef::kLinkOffset);
}

RawObject RawWeakRef::dequeue(RawObject* tail) {
  return dequeueReference(tail, RawWeakRef::kLinkOffset);
}

// Append tail2 to tail1 and return the new tail.
RawObject RawWeakRef::spliceQueue(RawObject tail1, RawObject tail2) {
  if (tail1 == RawNoneType::object() && tail2 == RawNoneType::object()) {
    return RawNoneType::object();
  }
  if (tail1 == RawNoneType::object()) {
    return tail2;
  }
  if (tail2 == RawNoneType::object()) {
    return tail1;
  }
  // merge two list, tail1 -> head2 -> ... -> tail2 -> head1
  RawObject head1 = RawWeakRef::cast(tail1).link();
  RawObject head2 = RawWeakRef::cast(tail2).link();
  RawWeakRef::cast(tail1).setLink(head2);
  RawWeakRef::cast(tail2).setLink(head1);
  return tail2;
}

// RawNativeProxy

void RawNativeProxy::enqueue(RawObject reference, RawObject* tail) {
  DCHECK(Thread::current()->runtime()->isNativeProxy(reference),
         "expected native proxy");
  enqueueReference(reference, tail, RawNativeProxy::kLinkOffset);
}

RawObject RawNativeProxy::dequeue(RawObject* tail) {
  DCHECK(Thread::current()->runtime()->isNativeProxy(*tail),
         "expected native proxy");
  return dequeueReference(tail, RawNativeProxy::kLinkOffset);
}

// RawHeapFrame

word RawHeapFrame::numAttributes(word extra_words) {
  return kNumOverheadWords + Frame::kSize / kPointerSize + extra_words;
}

word RawHeapFrame::virtualPC() const { return frame()->virtualPC(); }

void RawHeapFrame::setVirtualPC(word value) const {
  return frame()->setVirtualPC(value);
}

RawObject* RawHeapFrame::valueStackTop() const {
  return frame()->stashedValueStackTop();
}

RawObject RawHeapFrame::popValue() const { return frame()->stashedPopValue(); }

void RawHeapFrame::stashInternalPointers(Frame* original_frame) const {
  frame()->stashInternalPointers(original_frame);
}

}  // namespace py
