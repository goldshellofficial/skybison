# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
# Toolchain settings for a regular (non-Sandcastle) Linux build

set(SYSCONFIGDATA ${CMAKE_CURRENT_LIST_DIR}/linux/_sysconfigdata__linux_.py)

set(PYTHON python3.8)

set(BZIP2_LIBRARIES bz2)
set(FFI_LIBRARIES ffi)
set(NCURSES_LIBRARIES ncurses tinfo)
set(OPENSSL_PREFIX /usr)
set(OPENSSL_LIBRARIES crypto ssl)
set(READLINE_LIBRARIES readline)
set(SQLITE_LIBRARIES sqlite3)
set(XZ_LIBRARIES lzma)
set(ZLIB_LIBRARIES z)
