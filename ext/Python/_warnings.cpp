// _warnings.c implementation

#include "cpython-types.h"
#include "utils.h"

namespace python {

extern "C" int PyErr_WarnEx(PyObject*, const char*, Py_ssize_t) {
  UNIMPLEMENTED("PyErr_WarnEx");
}

}  // namespace python
