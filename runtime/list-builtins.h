#pragma once

#include "frame.h"
#include "globals.h"
#include "objects.h"
#include "thread.h"

namespace python {

Object* builtinListAppend(Thread* thread, Frame* frame, word nargs);
Object* builtinListInsert(Thread* thread, Frame* frame, word nargs);
Object* builtinListLen(Thread* thread, Frame* frame, word nargs);
Object* builtinListNew(Thread* thread, Frame* frame, word nargs);
Object* builtinListPop(Thread* thread, Frame* frame, word nargs);
Object* builtinListRemove(Thread* thread, Frame* frame, word nargs);

} // namespace python
