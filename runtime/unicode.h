#pragma once

#include <cstdint>

#include "globals.h"
#include "utils.h"

namespace py {

// Functions for ASCII code points. These should only be used for bytes-like
// objects or when a code point is guaranteed to be valid ASCII.
class ASCII {
 public:
  static bool isAlnum(byte b);
  static bool isAlpha(byte b);
  static bool isDecimal(byte b);
  static bool isDigit(byte b);
  static bool isLower(byte b);
  static bool isNumeric(byte b);
  static bool isPrintable(byte b);
  static bool isUpper(byte b);
  static bool isSpace(byte b);
  static bool isXidContinue(byte b);
  static bool isXidStart(byte b);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ASCII);
};

// Functions for Unicode code points.
class Unicode {
 public:
  // Predicates
  static bool isASCII(int32_t code_point);
  static bool isAlpha(int32_t code_point);
  static bool isPrintable(int32_t code_point);
  static bool isSpace(int32_t code_point);

  // Conversion
  static int32_t toLower(int32_t code_point);
  static int32_t toTitle(int32_t code_point);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Unicode);
};

// ASCII

inline bool ASCII::isAlnum(byte b) { return isDigit(b) || isAlpha(b); }

inline bool ASCII::isAlpha(byte b) { return isUpper(b) || isLower(b); }

inline bool ASCII::isDecimal(byte b) { return isDigit(b); }

inline bool ASCII::isDigit(byte b) { return '0' <= b && b <= '9'; }

inline bool ASCII::isLower(byte b) { return 'a' <= b && b <= 'z'; }

inline bool ASCII::isNumeric(byte b) { return isDigit(b); }

inline bool ASCII::isPrintable(byte b) { return ' ' <= b && b < kMaxASCII; }

inline bool ASCII::isSpace(byte b) {
  switch (b) {
    case '\t':
    case '\n':
    case '\x0B':
    case '\x0C':
    case '\r':
    case '\x1C':
    case '\x1D':
    case '\x1E':
    case '\x1F':
    case ' ':
      return true;
    default:
      return false;
  }
}

inline bool ASCII::isUpper(byte b) { return 'A' <= b && b <= 'Z'; }

inline bool ASCII::isXidContinue(byte b) { return isXidStart(b) || isDigit(b); }

inline bool ASCII::isXidStart(byte b) { return isAlpha(b) || b == '_'; }

// Unicode

inline bool Unicode::isASCII(int32_t code_point) {
  return code_point <= kMaxASCII;
}

inline bool Unicode::isAlpha(int32_t code_point) {
  if (isASCII(code_point)) {
    return ASCII::isAlpha(code_point);
  }
  // TODO(T57791326) support non-ASCII
  UNIMPLEMENTED("non-ASCII characters");
}

inline bool Unicode::isPrintable(int32_t code_point) {
  // TODO(T55176519): implement using Unicode database
  if (isASCII(code_point)) {
    return ASCII::isPrintable(code_point);
  }
  return true;
}

// Returns true for Unicode characters having the bidirectional
// type 'WS', 'B' or 'S' or the category 'Zs', false otherwise.
inline bool Unicode::isSpace(int32_t code_point) {
  if (isASCII(code_point)) {
    return ASCII::isSpace(code_point);
  }
  switch (code_point) {
    case 0x0085:
    case 0x00A0:
    case 0x1680:
    case 0x2000:
    case 0x2001:
    case 0x2002:
    case 0x2003:
    case 0x2004:
    case 0x2005:
    case 0x2006:
    case 0x2007:
    case 0x2008:
    case 0x2009:
    case 0x200A:
    case 0x2028:
    case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:
      return true;
    default:
      return false;
  }
}

inline int32_t Unicode::toLower(int32_t code_point) {
  if (isASCII(code_point)) {
    if (ASCII::isUpper(code_point)) {
      code_point -= ('A' - 'a');
    }
    return code_point;
  }
  // TODO(T57791326) support non-ASCII
  UNIMPLEMENTED("non-ASCII characters");
}

inline int32_t Unicode::toTitle(int32_t code_point) {
  if (isASCII(code_point)) {
    if (ASCII::isLower(code_point)) {
      code_point += ('A' - 'a');
    }
    return code_point;
  }
  // TODO(T57791326) support non-ASCII
  UNIMPLEMENTED("non-ASCII characters");
}

}  // namespace py
