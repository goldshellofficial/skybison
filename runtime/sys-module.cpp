#include "sys-module.h"
#include <unistd.h>

#include "builtins-module.h"
#include "frame.h"
#include "frozen-modules.h"
#include "globals.h"
#include "handles.h"
#include "objects.h"
#include "os.h"
#include "runtime.h"
#include "thread.h"

namespace python {

extern "C" struct _inittab _PyImport_Inittab[];

void SysModule::postInitialize(Thread* thread, Runtime* runtime,
                               const Module& module) {
  HandleScope scope(thread);
  Object modules(&scope, runtime->modules_);
  runtime->moduleAddGlobal(module, SymbolId::kModules, modules);

  runtime->display_hook_ = runtime->moduleAddBuiltinFunction(
      module, SymbolId::kDisplayhook, displayhook);

  // Fill in sys...
  Object stdout_val(&scope, SmallInt::fromWord(STDOUT_FILENO));
  runtime->moduleAddGlobal(module, SymbolId::kStdout, stdout_val);

  Object stderr_val(&scope, SmallInt::fromWord(STDERR_FILENO));
  runtime->moduleAddGlobal(module, SymbolId::kStderr, stderr_val);

  Object meta_path(&scope, runtime->newList());
  runtime->moduleAddGlobal(module, SymbolId::kMetaPath, meta_path);

  Object path(&scope, initialSysPath(Thread::currentThread()));
  runtime->moduleAddGlobal(module, SymbolId::kPath, path);

  Object platform(&scope, runtime->newStrFromCStr(OS::name()));
  runtime->moduleAddGlobal(module, SymbolId::kPlatform, platform);

  // Count the number of modules and create a tuple
  uword num_external_modules = 0;
  while (_PyImport_Inittab[num_external_modules].name != nullptr) {
    num_external_modules++;
  }
  uword num_builtin_modules = 0;
  for (; Runtime::kBuiltinModules[num_builtin_modules].name !=
         SymbolId::kSentinelId;
       num_builtin_modules++) {
  }

  uword num_modules = num_builtin_modules + num_external_modules;
  Tuple builtins_tuple(&scope, runtime->newTuple(num_modules));

  // Add all the available builtin modules
  for (uword i = 0; i < num_builtin_modules; i++) {
    Object module_name(
        &scope, runtime->symbols()->at(Runtime::kBuiltinModules[i].name));
    builtins_tuple.atPut(i, *module_name);
  }

  // Add all the available extension builtin modules
  for (int i = 0; _PyImport_Inittab[i].name != nullptr; i++) {
    Object module_name(&scope,
                       runtime->newStrFromCStr(_PyImport_Inittab[i].name));
    builtins_tuple.atPut(num_builtin_modules + i, *module_name);
  }

  // Create builtin_module_names tuple
  Object builtins(&scope, *builtins_tuple);
  runtime->moduleAddGlobal(module, SymbolId::kBuiltinModuleNames, builtins);

  runtime->executeModule(kSysModuleData, module);
}

RawObject SysModule::displayhook(Thread* thread, Frame* frame, word nargs) {
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object obj(&scope, args.get(0));
  if (obj.isNoneType()) {
    return NoneType::object();
  }
  UNIMPLEMENTED("sys.displayhook()");
}

RawObject initialSysPath(Thread* thread) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  List result(&scope, runtime->newList());
  Object empty_string(&scope, runtime->newStrWithAll(View<byte>(nullptr, 0)));
  runtime->listAdd(result, empty_string);

  const char* python_path = getenv("PYTHONPATH");
  if (!python_path || python_path[0] == '\0') return *result;

  // TODO(T39226962): We should rewrite this in python so we have path
  // manipulation helpers available. Current limitations:
  // - Does not transform relative paths to absolute ones.
  // - Does not normalize paths.
  // - Does not filter out duplicate paths.
  Object path(&scope, NoneType::object());
  const char* c = python_path;
  do {
    const char* segment_begin = c;
    // Advance to the next delimiter or end of string.
    while (*c != ':' && *c != '\0') {
      c++;
    }

    View<byte> path_bytes(reinterpret_cast<const byte*>(segment_begin),
                          c - segment_begin);
    CHECK(path_bytes.data()[0] == '/',
          "relative paths in PYTHONPATH not supported yet");
    path = runtime->newStrWithAll(path_bytes);
    runtime->listAdd(result, path);
  } while (*c++ != '\0');
  return *result;
}

}  // namespace python
