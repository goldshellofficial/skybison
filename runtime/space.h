/* Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com) */
#pragma once

#include "globals.h"
#include "utils.h"

namespace py {

class Space {
 public:
  explicit Space(word size);
  ~Space();

  bool allocate(word size, uword* result);

  void protect();

  void unprotect();

  bool contains(uword address) { return address >= start() && address < end(); }

  bool isAllocated(uword address) {
    return address >= start() && address < fill();
  }

  uword start() { return start_; }

  uword end() { return end_; }

  uword fill() { return fill_; }

  void reset();

  word size() { return end_ - start_; }

  static int endOffset() { return offsetof(Space, end_); }

  static int fillOffset() { return offsetof(Space, fill_); }

 private:
  uword start_;
  uword end_;
  uword fill_;

  byte* raw_;

  DISALLOW_COPY_AND_ASSIGN(Space);
};

inline bool Space::allocate(word size, uword* result) {
  word fill = fill_;
  word free = end_ - fill;
  if (size > free) {
    return false;
  }
  *result = fill;
  fill_ = fill + size;
  return true;
}

}  // namespace py
