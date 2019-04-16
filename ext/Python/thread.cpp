#include "Python.h"
#include "pythread.h"

#include "globals.h"
#include "mutex.h"
#include "utils.h"

namespace python {

PY_EXPORT PyThread_type_lock PyThread_allocate_lock(void) {
  return static_cast<void*>(new Mutex());
}

PY_EXPORT void PyThread_free_lock(PyThread_type_lock lock) {
  delete static_cast<Mutex*>(lock);
}

PY_EXPORT int PyThread_acquire_lock(PyThread_type_lock lock, int waitflag) {
  DCHECK(waitflag == WAIT_LOCK || waitflag == NOWAIT_LOCK,
         "waitflag should either be WAIT_LOCK or NOWAIT_LOCK");
  if (waitflag == WAIT_LOCK) {
    static_cast<Mutex*>(lock)->lock();
    return PY_LOCK_ACQUIRED;
  }
  return static_cast<Mutex*>(lock)->tryLock();
}

PY_EXPORT void PyThread_release_lock(PyThread_type_lock lock) {
  static_cast<Mutex*>(lock)->unlock();
}

}  // namespace python
