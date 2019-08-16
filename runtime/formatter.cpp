#include "formatter.h"

#include "runtime.h"

namespace python {

static bool isAlignmentSpec(int32_t cp) {
  switch (cp) {
    case '<':
    case '>':
    case '=':
    case '^':
      return true;
    default:
      return false;
  }
}

static inline int32_t nextCodePoint(const Str& spec, word length, word* index) {
  if (*index >= length) {
    return 0;
  }
  word cp_length;
  int32_t cp = spec.codePointAt(*index, &cp_length);
  *index += cp_length;
  return cp;
}

RawObject parseFormatSpec(Thread* thread, const Str& spec, int32_t default_type,
                          char default_align, FormatSpec* result) {
  result->alignment = default_align;
  result->positive_sign = 0;
  result->thousands_separator = 0;
  result->type = default_type;
  result->alternate = false;
  result->fill_char = ' ';
  result->width = -1;
  result->precision = -1;

  word index = 0;
  word length = spec.charLength();
  int32_t cp = nextCodePoint(spec, length, &index);

  bool fill_char_specified = false;
  bool alignment_specified = false;
  word old_index = index;
  int32_t c_next = nextCodePoint(spec, length, &index);
  if (isAlignmentSpec(c_next)) {
    result->alignment = static_cast<char>(c_next);
    result->fill_char = cp;
    fill_char_specified = true;
    alignment_specified = true;

    cp = nextCodePoint(spec, length, &index);
  } else if (!alignment_specified && isAlignmentSpec(cp)) {
    result->alignment = static_cast<char>(cp);
    alignment_specified = true;
    cp = c_next;
  } else {
    index = old_index;
  }

  switch (cp) {
    case '+':
    case ' ':
      result->positive_sign = static_cast<char>(cp);
      cp = nextCodePoint(spec, length, &index);
      break;
    case '-':
      cp = nextCodePoint(spec, length, &index);
      break;
  }

  if (!fill_char_specified && cp == '0') {
    result->fill_char = '0';
    if (!alignment_specified) {
      result->alignment = '=';
    }
  }

  if (cp == '#') {
    result->alternate = true;
    cp = nextCodePoint(spec, length, &index);
  }

  if ('0' <= cp && cp <= '9') {
    word width = 0;
    for (;;) {
      width += cp - '0';
      cp = nextCodePoint(spec, length, &index);
      if ('0' > cp || cp > '9') break;
      if (__builtin_mul_overflow(width, 10, &width)) {
        return thread->raiseWithFmt(LayoutId::kValueError,
                                    "Too many decimal digits in format string");
      }
    }
    result->width = width;
  }

  if (cp == ',') {
    result->thousands_separator = ',';
    cp = nextCodePoint(spec, length, &index);
  }
  if (cp == '_') {
    if (result->thousands_separator != 0) {
      return thread->raiseWithFmt(LayoutId::kValueError,
                                  "Cannot specify both ',' and '_'.");
    }
    result->thousands_separator = '_';
    cp = nextCodePoint(spec, length, &index);
  }
  if (cp == ',') {
    return thread->raiseWithFmt(LayoutId::kValueError,
                                "Cannot specify both ',' and '_'.");
  }

  if (cp == '.') {
    cp = nextCodePoint(spec, length, &index);
    if ('0' > cp || cp > '9') {
      return thread->raiseWithFmt(LayoutId::kValueError,
                                  "Format specifier missing precision");
    }

    word precision = 0;
    for (;;) {
      precision += cp - '0';
      cp = nextCodePoint(spec, length, &index);
      if ('0' > cp || cp > '9') break;
      if (__builtin_mul_overflow(precision, 10, &precision)) {
        return thread->raiseWithFmt(LayoutId::kValueError,
                                    "Too many decimal digits in format string");
      }
    }
    result->precision = precision;
  }

  if (cp != 0) {
    result->type = cp;
    // This was the last step: No need to call `nextCodePoint()` here.
  }
  if (index < length) {
    return thread->raiseWithFmt(LayoutId::kValueError,
                                "Invalid format specifier");
  }

  if (result->thousands_separator) {
    switch (result->type) {
      case 'd':
      case 'e':
      case 'f':
      case 'g':
      case 'E':
      case 'G':
      case '%':
      case 'F':
      case '\0':
        // These are allowed. See PEP 378.
        break;
      case 'b':
      case 'o':
      case 'x':
      case 'X':
        // Underscores are allowed in bin/oct/hex. See PEP 515.
        if (result->thousands_separator == '_') {
          break;
        }
        /* fall through */
      default:
        if (32 < result->type && result->type <= kMaxASCII) {
          return thread->raiseWithFmt(
              LayoutId::kValueError, "Cannot specify '%c' with '%c'.",
              result->thousands_separator, static_cast<char>(result->type));
        }
        return thread->raiseWithFmt(
            LayoutId::kValueError, "Cannot specify '%c' with '\\x%x'.",
            result->thousands_separator, static_cast<unsigned>(result->type));
    }
  }
  return NoneType::object();
}

RawObject formatStr(Thread* thread, const Str& str, FormatSpec* format) {
  if (format->positive_sign != '\0') {
    return thread->raiseWithFmt(LayoutId::kValueError,
                                "Sign not allowed in string format specifier");
  }
  if (format->alternate) {
    return thread->raiseWithFmt(
        LayoutId::kValueError,
        "Alternate form (#) not allowed in string format specifier");
  }
  if (format->alignment == '=') {
    return thread->raiseWithFmt(
        LayoutId::kValueError,
        "'=' alignment not allowed in string format specifier");
  }
  word width = format->width;
  word precision = format->precision;
  if (width == -1 && precision == -1) {
    return *str;
  }

  word char_length = str.charLength();
  word codepoint_length;
  word str_end_index;
  if (precision >= 0) {
    str_end_index = str.offsetByCodePoints(0, precision);
    if (str_end_index < char_length) {
      codepoint_length = precision;
    } else {
      codepoint_length = str.codePointLength();
    }
  } else {
    str_end_index = char_length;
    codepoint_length = str.codePointLength();
  }

  Runtime* runtime = thread->runtime();
  word padding = width - codepoint_length;
  if (padding <= 0) {
    return runtime->strSubstr(thread, str, 0, str_end_index);
  }

  // Construct result.
  HandleScope scope(thread);
  Str fill_char(&scope, SmallStr::fromCodePoint(format->fill_char));
  word fill_char_length = fill_char.charLength();
  word padding_char_length = padding * fill_char_length;
  word result_char_length = str_end_index + padding_char_length;
  MutableBytes result(
      &scope, runtime->newMutableBytesUninitialized(result_char_length));
  word index = 0;
  word early_padding;
  if (format->alignment == '>') {
    early_padding = padding;
    padding = 0;
  } else if (format->alignment == '^') {
    word half = padding / 2;
    early_padding = half;
    padding = half + (padding % 2);
  } else {
    early_padding = 0;
    DCHECK(format->alignment == '<', "remaining assignment must be '<'");
  }
  for (word i = 0; i < early_padding; i++) {
    result.replaceFromWithStr(index, *fill_char, fill_char_length);
    index += fill_char_length;
  }
  result.replaceFromWithStr(index, *str, str_end_index);
  index += str_end_index;
  if (padding > 0) {
    DCHECK(format->alignment == '<' || format->alignment == '^',
           "unexpected alignment");
    for (word i = 0; i < padding; i++) {
      result.replaceFromWithStr(index, *fill_char, fill_char_length);
      index += fill_char_length;
    }
  }
  DCHECK(index == result_char_length, "overflow or underflow in result");
  return result.becomeStr();
}

}  // namespace python
