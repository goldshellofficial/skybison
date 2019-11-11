#include "builtins-module.h"
#include "capi-handles.h"
#include "cpython-data.h"
#include "cpython-func.h"
#include "cpython-types.h"
#include "globals.h"
#include "runtime.h"
#include "str-builtins.h"
#include "thread.h"
#include "utils.h"

// Declarations from `Python-ast.h` and `ast.h` which are not part of
// `Python.h` so we shouldn't add them to `cpython-*.h`.
extern "C" {
mod_ty PyAST_FromNode(const _node*, PyCompilerFlags*, const char*, PyArena*);
PyObject* PyAST_mod2obj(mod_ty t);
enum _mod_kind {
  Module_kind = 1,
  Interactive_kind = 2,
  Expression_kind = 3,
  Suite_kind = 4
};
struct _mod {
  enum _mod_kind kind;
  void* dummy;
};
}

namespace py {

static_assert(Code::kCompileFlagsMask == PyCF_MASK, "flags mismatch");

PY_EXPORT PyObject* _Py_Mangle(PyObject* pyprivateobj, PyObject* pyident) {
  if (pyprivateobj == nullptr) {
    ApiHandle::fromPyObject(pyident)->incref();
    return pyident;
  }
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  Object privateobj_obj(&scope,
                        ApiHandle::fromPyObject(pyprivateobj)->asObject());
  Object ident_obj(&scope, ApiHandle::fromPyObject(pyident)->asObject());
  Str ident(&scope, strUnderlying(thread, ident_obj));
  Runtime* runtime = thread->runtime();
  // Only mangle names that start with two underscores, but do not end with
  // two underscores or contain a dot.
  word ident_length = ident.charLength();
  if (!runtime->isInstanceOfStr(*privateobj_obj) || ident_length < 2 ||
      ident.charAt(0) != '_' || ident.charAt(1) != '_' ||
      (ident.charAt(ident_length - 2) == '_' &&
       ident.charAt(ident_length - 1) == '_') ||
      strFindAsciiChar(ident, '.') >= 0) {
    Py_INCREF(pyident);
    return pyident;
  }

  Str privateobj(&scope, strUnderlying(thread, privateobj_obj));
  word privateobj_length = privateobj.charLength();
  word begin = 0;
  while (begin < privateobj_length && privateobj.charAt(begin) == '_') {
    begin++;
  }
  if (begin == privateobj_length) {
    Py_INCREF(pyident);
    return pyident;
  }

  word length0 = privateobj_length - begin;
  word length = length0 + ident_length + 1;
  MutableBytes result(&scope, runtime->newMutableBytesUninitialized(length));
  result.byteAtPut(0, '_');
  result.replaceFromWithStrStartAt(1, *privateobj, length0, begin);
  result.replaceFromWithStr(1 + length0, *ident, ident_length);
  return ApiHandle::newReference(thread, result.becomeStr());
}

PY_EXPORT PyCodeObject* PyNode_Compile(_node* node, const char* filename) {
  PyArena* arena = PyArena_New();
  if (!arena) return nullptr;
  _mod* mod = PyAST_FromNode(node, nullptr, filename, arena);
  if (mod == nullptr) {
    PyArena_Free(arena);
    return nullptr;
  }
  PyCodeObject* code = PyAST_Compile(mod, filename, nullptr, arena);
  PyArena_Free(arena);
  return code;
}

PY_EXPORT PyCodeObject* PyAST_Compile(_mod* mod, const char* filename,
                                      PyCompilerFlags* flags, PyArena* arena) {
  return PyAST_CompileEx(mod, filename, flags, -1, arena);
}

PY_EXPORT PyCodeObject* PyAST_CompileEx(_mod* mod, const char* filename_str,
                                        PyCompilerFlags* flags, int optimize,
                                        PyArena* arena) {
  PyObject* filename = PyUnicode_DecodeFSDefault(filename_str);
  if (filename == nullptr) return nullptr;
  PyCodeObject* co = PyAST_CompileObject(mod, filename, flags, optimize, arena);
  Py_DECREF(filename);
  return co;
}

PY_EXPORT PyCodeObject* PyAST_CompileObject(_mod* mod, PyObject* pyfilename,
                                            PyCompilerFlags* flags_ptr,
                                            int optimize, PyArena*) {
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  PyObject* pyast = PyAST_mod2obj(mod);
  Object ast(&scope, ApiHandle::fromPyObject(pyast)->asObject());
  Object filename(&scope, ApiHandle::fromPyObject(pyfilename)->asObject());
  SymbolId mode_id;
  switch (mod->kind) {
    case Module_kind:
      mode_id = SymbolId::kExec;
      break;
    case Interactive_kind:
      mode_id = SymbolId::kSingle;
      break;
    case Expression_kind:
      mode_id = SymbolId::kEval;
      break;
    default:
      UNREACHABLE("Unknown module kind");
  }
  word flags = flags_ptr ? flags_ptr->cf_flags : 0;
  Object result(&scope,
                compile(thread, ast, filename, mode_id, flags, optimize));
  if (result.isErrorException()) return nullptr;
  return reinterpret_cast<PyCodeObject*>(
      ApiHandle::newReference(thread, *result));
}

PY_EXPORT int PyCompile_OpcodeStackEffect(int /*opcode*/, int /*oparg*/) {
  UNIMPLEMENTED("PyCompile_OpcodeStackEffect");
}

}  // namespace py
