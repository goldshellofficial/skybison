/* prepare_protocol.c - the protocol for preparing values for SQLite
 *
 * Copyright (C) 2005-2010 Gerhard Häring <gh@ghaering.de>
 *
 * This file is part of pysqlite.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include "module.h"  /* TODO(T71639364): remove once MODULE_NAME exists in environment */
#include "prepare_protocol.h"

int pysqlite_prepare_protocol_init(pysqlite_PrepareProtocol* self, PyObject* args, PyObject* kwargs)
{
    return 0;
}

void pysqlite_prepare_protocol_dealloc(pysqlite_PrepareProtocol* self)
{
    PyTypeObject* tp = Py_TYPE(self);
    freefunc func = PyType_GetSlot(tp, Py_tp_free);
    Py_DECREF(tp);
    func(self);
}

static PyType_Slot pysqlite_PrepareProtocolType_slots[] = {
    {Py_tp_dealloc, pysqlite_prepare_protocol_dealloc},
    {Py_tp_init, pysqlite_prepare_protocol_init},
    {Py_tp_new, PyType_GenericNew},
    {0, 0},
};

static PyType_Spec pysqlite_PrepareProtocolType_spec = {
        MODULE_NAME ".PrepareProtocol",
        sizeof(pysqlite_PrepareProtocol),
        0,
        Py_TPFLAGS_DEFAULT,
        pysqlite_PrepareProtocolType_slots,
};

extern PyObject* pysqlite_setup_PrepareProtocolType(void)
{
    return PyType_FromSpec(&pysqlite_PrepareProtocolType_spec);
}
