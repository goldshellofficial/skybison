#pragma once

#include <cstddef>
#include <cstdint>

typedef unsigned char byte;
typedef signed char sbyte;
typedef short int int16;
typedef int int32;
typedef intptr_t word;
typedef uintptr_t uword;

const int kWordSize = sizeof(word);
const int kPointerSize = sizeof(void*);
const int kDoubleSize = sizeof(double);

const int kBitsPerByte = 8;
const int kBitsPerPointer = kBitsPerByte * kWordSize;

const word kMinWord = INTPTR_MIN;
const word kMaxWord = INTPTR_MAX;

const int KiB = 1024;
const int MiB = KiB * KiB;
const int GiB = KiB * KiB * KiB;

const int kMillisecondsPerSecond = 1000;
const int kMillsecondsPerMicrosecond = 1000;
const int kMicrosecondsPerSecond =
    kMillisecondsPerSecond * kMillsecondsPerMicrosecond;
const int kNanosecondsPerMicrosecond = 1000;
const int kNanosecondsPerSecond =
    kMicrosecondsPerSecond * kNanosecondsPerMicrosecond;

#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))

#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                     \
  TypeName(const TypeName&) = delete;                                          \
  void operator=(const TypeName&) = delete

#define DISALLOW_HEAP_ALLOCATION()                                             \
  void* operator new(size_t size) = delete;                                    \
  void operator delete(void* p) = delete

#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName)                               \
  TypeName() = delete;                                                         \
  DISALLOW_COPY_AND_ASSIGN(TypeName)

#define OFFSET_OF(type, field)                                                 \
  (reinterpret_cast<intptr_t>(&(reinterpret_cast<type*>(16)->field)) - 16)

#if __GNUG__ && __GNUC__ < 5
#define IS_TRIVIALLY_COPYABLE(T) __has_trivial_copy(T)
#else
#include <type_traits>
#define IS_TRIVIALLY_COPYABLE(T) std::is_trivially_copyable<T>::value
#endif

// Branch prediction hints for the compiler.  Use in performance critial code
// which almost always branches one way.
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
