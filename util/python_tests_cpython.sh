#!/bin/bash

if [[ -z $BUILD_DIR ]]; then
  BUILD_DIR="$(dirname "$0")/../build"
fi

# Add .exe for cpython's MacOS binary
CPYTHON_BIN="$BUILD_DIR/third-party/cpython/python"
if [[ "$(uname)" == "Darwin" ]]; then
  CPYTHON_BIN+=".exe"
fi

if [[ ! -x "$CPYTHON_BIN" ]]; then
  echo "$CPYTHON_BIN is not executable. Please build using 'make compile-cpython'" 1>&2
  exit 1
fi

BUILD_DIR="$BUILD_DIR" PYTHON_BIN="$CPYTHON_BIN" FIND_FILTER="[a-zA-Z]*_test.py" \
  "$(dirname "$0")/python_tests_pyro.sh" "$@"
