// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "valgrind/callgrind.h"

#include "builtins.h"
#include "frame.h"
#include "handles.h"
#include "modules.h"
#include "runtime.h"
#include "thread.h"

namespace py {

RawObject FUNC(_valgrind, callgrind_dump_stats)(Thread* thread,
                                                Arguments args) {
  HandleScope scope(thread);
  Object description(&scope, args.get(0));
  if (description.isNoneType()) {
    CALLGRIND_DUMP_STATS;
    return NoneType::object();
  }
  if (!thread->runtime()->isInstanceOfStr(*description)) {
    return thread->raiseRequiresType(description, ID(str));
  }
  Str description_str(&scope, strUnderlying(*description));
  unique_c_ptr<char> description_cstr(description_str.toCStr());
  CALLGRIND_DUMP_STATS_AT(description_cstr.get());
  return NoneType::object();
}

RawObject FUNC(_valgrind, callgrind_start_instrumentation)(Thread*, Arguments) {
  CALLGRIND_START_INSTRUMENTATION;
  return NoneType::object();
}

RawObject FUNC(_valgrind, callgrind_stop_instrumentation)(Thread*, Arguments) {
  CALLGRIND_STOP_INSTRUMENTATION;
  return NoneType::object();
}

RawObject FUNC(_valgrind, callgrind_zero_stats)(Thread*, Arguments) {
  CALLGRIND_ZERO_STATS;
  return NoneType::object();
}

}  // namespace py
