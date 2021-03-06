// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "builtins.h"
#include "frame.h"
#include "handles.h"
#include "modules.h"
#include "runtime.h"
#include "thread.h"
#include "type-builtins.h"

namespace py {

namespace {
RawObject getCategory(Thread* thread, const Object& message,
                      const Object& category) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);

  Type result(&scope, runtime->typeOf(*message));
  Type warning(&scope, runtime->typeAt(LayoutId::kWarning));
  // TODO(bsimmers): Use our equivalent of PyObject_IsInstance once we have it.
  if (!typeIsSubclass(*result, *warning)) {
    if (category.isNoneType()) {
      result = *warning;
    } else if (runtime->isInstanceOfType(*category)) {
      result = *category;
    }
    if (!typeIsSubclass(*result, *warning)) {
      return thread->raiseWithFmt(LayoutId::kTypeError,
                                  "category must be a Warning subclass");
    }
  }

  return *result;
}
}  // namespace

RawObject FUNC(_warnings, warn)(Thread* thread, Arguments args) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  Object message(&scope, args.get(0));
  Object category(&scope, args.get(1));
  Object stacklevel(&scope, args.get(2));

  if (!runtime->isInstanceOfInt(*stacklevel)) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "integer argument expected");
  }
  auto result = intUnderlying(*stacklevel).asInt<word>();
  if (result.error != CastError::None) {
    return thread->raiseWithFmt(LayoutId::kOverflowError,
                                "Python int too large to convert to C ssize_t");
  }

  Object real_category(&scope, getCategory(thread, message, category));
  if (real_category.isError()) return *real_category;

  // TODO(T39431178): Implement proper filtering/escalation.
  return NoneType::object();
}

}  // namespace py
