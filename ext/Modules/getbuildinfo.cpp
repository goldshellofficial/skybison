#include "cpython-func.h"

#include "version.h"

namespace py {

PY_EXPORT const char* Py_GetBuildInfo() { return buildInfo(); }

}  // namespace py
