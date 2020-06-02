#include "cpython-func.h"

#include "capi-handles.h"
#include "module-builtins.h"
#include "modules.h"
#include "runtime.h"

namespace py {

PY_EXPORT int PyGILState_Check() {
  // TODO(T44861733): Make this do something intelligent
  CHECK(Thread::current()->next() == nullptr,
        "PyGILState_Check doesn't currently work with more than one thread");
  return 1;
}

PY_EXPORT PyGILState_STATE PyGILState_Ensure() {
  // TODO(T44861733): Make this do something intelligent
  return PyGILState_LOCKED;
}

PY_EXPORT PyThreadState* PyGILState_GetThisThreadState() {
  UNIMPLEMENTED("PyGILState_GetThisThreadState");
}

PY_EXPORT void PyGILState_Release(PyGILState_STATE /* e */) {
  // TODO(T44861733): Make this do something intelligent
}

PY_EXPORT void PyInterpreterState_Clear(PyInterpreterState* /* p */) {
  UNIMPLEMENTED("PyInterpreterState_Clear");
}

PY_EXPORT void PyInterpreterState_Delete(PyInterpreterState* /* p */) {
  UNIMPLEMENTED("PyInterpreterState_Delete");
}

static int moduleListAdd(Thread* thread, PyObject* module, PyModuleDef* def) {
  if (def == nullptr) {
    DCHECK(PyErr_Occurred() != nullptr, "expected raised error");
    return -1;
  }
  if (def->m_slots != nullptr) {
    thread->raiseWithFmt(LayoutId::kSystemError,
                         "PyState_AddModule called on module with slots");
    return -1;
  }
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Module module_obj(&scope, ApiHandle::fromPyObject(module)->asObject());
  module_obj.setDef(runtime->newIntFromCPtr(def));
  return runtime->moduleListAtPut(thread, module_obj, def->m_base.m_index) ? 0
                                                                           : -1;
}

PY_EXPORT int PyState_AddModule(PyObject* module, PyModuleDef* def) {
  DCHECK(module != nullptr, "module must not be null");
  if (def == nullptr) {
    Py_FatalError("PyState_AddModule: Module Definition is NULL");
    return -1;
  }
  Thread* thread = Thread::current();
  Runtime* runtime = thread->runtime();
  if (!runtime->moduleListAt(thread, def->m_base.m_index).isErrorNotFound()) {
    Py_FatalError("PyState_AddModule: Module already added!");
    return -1;
  }
  return moduleListAdd(thread, module, def);
}

PY_EXPORT PyObject* PyState_FindModule(PyModuleDef* def) {
  if (def->m_slots != nullptr) {
    return nullptr;
  }
  Py_ssize_t index = def->m_base.m_index;
  if (index == 0) {
    return nullptr;
  }
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Object module_obj(&scope, runtime->moduleListAt(thread, index));
  if (module_obj.isErrorNotFound()) {
    return nullptr;
  }
  return ApiHandle::borrowedReference(thread, *module_obj);
}

PY_EXPORT int PyState_RemoveModule(PyModuleDef* /* f */) {
  UNIMPLEMENTED("PyState_RemoveModule");
}

PY_EXPORT void PyThreadState_Clear(PyThreadState* /* e */) {
  UNIMPLEMENTED("PyThreadState_Clear");
}

PY_EXPORT void PyThreadState_Delete(PyThreadState* /* e */) {
  UNIMPLEMENTED("PyThreadState_Delete");
}

PY_EXPORT void PyThreadState_DeleteCurrent() {
  UNIMPLEMENTED("PyThreadState_DeleteCurrent");
}

PY_EXPORT PyThreadState* PyThreadState_Get() {
  return reinterpret_cast<PyThreadState*>(Thread::current());
}

PY_EXPORT PyObject* PyThreadState_GetDict() {
  UNIMPLEMENTED("PyThreadState_GetDict");
}

PY_EXPORT PyThreadState* PyThreadState_New(PyInterpreterState* /* p */) {
  UNIMPLEMENTED("PyThreadState_New");
}

PY_EXPORT PyThreadState* PyThreadState_Next(PyThreadState* /* p */) {
  UNIMPLEMENTED("PyThreadState_Next");
}

PY_EXPORT int PyThreadState_SetAsyncExc(unsigned long /* d */,
                                        PyObject* /* c */) {
  UNIMPLEMENTED("PyThreadState_SetAsyncExc");
}

PY_EXPORT PyThreadState* PyThreadState_Swap(PyThreadState* /* s */) {
  UNIMPLEMENTED("PyThreadState_Swap");
}

PY_EXPORT void _PyGILState_Reinit() {
  // TODO(T39596544): do nothing until we have a GIL.
}

PY_EXPORT int _PyState_AddModule(PyObject* module, PyModuleDef* def) {
  Thread* thread = Thread::current();
  return moduleListAdd(thread, module, def);
}

PY_EXPORT void _PyThreadState_Init(PyThreadState* /* e */) {
  UNIMPLEMENTED("_PyThreadState_Init");
}

PY_EXPORT PyThreadState* _PyThreadState_Prealloc(PyInterpreterState* /* p */) {
  UNIMPLEMENTED("_PyThreadState_Prealloc");
}

PY_EXPORT PyInterpreterState* PyInterpreterState_Head() {
  UNIMPLEMENTED("PyInterpreterState_Head");
}

PY_EXPORT PyInterpreterState* PyInterpreterState_Next(
    PyInterpreterState* /* p */) {
  UNIMPLEMENTED("PyInterpreterState_Next");
}

PY_EXPORT PyThreadState* PyInterpreterState_ThreadHead(
    PyInterpreterState* /* p */) {
  UNIMPLEMENTED("PyInterpreterState_ThreadHead");
}

PY_EXPORT void _PyState_ClearModules() {
  UNIMPLEMENTED("_PyState_ClearModules");
}

PY_EXPORT int _PyThreadState_GetRecursionDepth(PyThreadState* ts) {
  Thread* thread = reinterpret_cast<Thread*>(ts);
  return thread->recursionDepth();
}

}  // namespace py
