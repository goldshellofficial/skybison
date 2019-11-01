#include "frame.h"

#include <cstring>

#include "dict-builtins.h"
#include "handles.h"
#include "objects.h"
#include "runtime.h"

namespace py {

const char* Frame::isInvalid() {
  if (!at(kPreviousFrameOffset).isSmallInt()) {
    return "bad previousFrame field";
  }
  if (!isSentinel() && !(locals() + 1)->isFunction()) {
    return "bad function";
  }
  return nullptr;
}

RawObject frameGlobals(Thread* thread, Frame* frame) {
  HandleScope scope(thread);
  // TODO(T36407403): avoid a reverse mapping by reading the module directly
  // out of the function object or the frame.
  Object name(&scope, frame->function().module());
  Object hash_obj(&scope, Interpreter::hash(thread, name));
  if (hash_obj.isErrorException()) return *hash_obj;
  word hash = SmallInt::cast(*hash_obj).value();

  Runtime* runtime = thread->runtime();
  Dict modules(&scope, runtime->modules());
  Object module_obj(&scope, dictAt(thread, modules, name, hash));
  if (module_obj.isErrorNotFound() ||
      !runtime->isInstanceOfModule(*module_obj)) {
    UNIMPLEMENTED("modules not registered in sys.modules");
  }
  Module module(&scope, *module_obj);
  return module.moduleProxy();
}

}  // namespace py
