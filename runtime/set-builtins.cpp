#include "set-builtins.h"

#include "frame.h"
#include "globals.h"
#include "objects.h"
#include "runtime.h"
#include "thread.h"

namespace python {

Object* builtinSetLen(Thread* thread, Frame* caller, word nargs) {
  if (nargs != 1) {
    return thread->throwTypeErrorFromCString("__len__() takes no arguments");
  }
  HandleScope scope(thread);
  Arguments args(caller, nargs);
  Handle<Object> self(&scope, args.get(0));
  if (self->isSet()) {
    return SmallInteger::fromWord(Set::cast(*self)->numItems());
  }
  // TODO(cshapiro): handle user-defined subtypes of set.
  return thread->throwTypeErrorFromCString("'__len__' requires a 'set' object");
}

Object* builtinSetPop(Thread* thread, Frame* caller, word nargs) {
  if (nargs != 1) {
    return thread->throwTypeErrorFromCString("pop() takes no arguments");
  }
  HandleScope scope(thread);
  Arguments args(caller, nargs);
  Handle<Set> self(&scope, args.get(0));
  if (self->isSet()) {
    Handle<ObjectArray> data(&scope, self->data());
    word num_items = self->numItems();
    if (num_items > 0) {
      for (word i = 0; i < data->length(); i += SetBucket::kNumPointers) {
        SetBucket bucket(data, i);
        if (bucket.isTombstone() || bucket.isEmpty())
          continue;
        Handle<Object> value(&scope, bucket.key());
        bucket.setTombstone();
        self->setNumItems(num_items - 1);
        return *value;
      }
    }
    return thread->throwKeyErrorFromCString("pop from an empty set");
  }
  // TODO(T30253711): handle user-defined subtypes of set.
  return thread->throwTypeErrorFromCString(
      "descriptor 'pop' requires a 'set' object");
}

} // namespace python
