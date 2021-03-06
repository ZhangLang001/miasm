#include <Python.h>
#include "structmember.h"
#include <stdint.h>
#include <inttypes.h>
#include "../queue.h"
#include "../vm_mngr.h"
#include "../vm_mngr_py.h"
#include "../JitCore.h"
#include "JitCore_ppc32.h"

reg_dict gpreg_dict[] = {
#define JITCORE_PPC_REG_EXPAND(_name, _size)				\
    { .name = #_name, .offset = offsetof(struct vm_cpu, _name) },
#include "JitCore_ppc32_regs.h"
#undef JITCORE_PPC_REG_EXPAND
};

PyObject* cpu_get_gpreg(JitCpu* self)
{
    PyObject *dict = PyDict_New();
    PyObject *o;

#define JITCORE_PPC_REG_EXPAND(_name, _size) \
    get_reg(_name);
#include "JitCore_ppc32_regs.h"
#undef JITCORE_PPC_REG_EXPAND

    return dict;
}



PyObject *
cpu_set_gpreg(JitCpu *self, PyObject *args) {
    PyObject *dict;
    PyObject *d_key, *d_value = NULL;
    Py_ssize_t pos = 0;
    uint64_t val;
    unsigned int i;

    if (!PyArg_ParseTuple(args, "O", &dict))
	return NULL;
    if(!PyDict_Check(dict))
	RAISE(PyExc_TypeError, "arg must be dict");

    while(PyDict_Next(dict, &pos, &d_key, &d_value)) {
	int found = 0;

	if(!PyString_Check(d_key))
	    RAISE(PyExc_TypeError, "key must be str");

	PyGetInt(d_value, val);

	for (i=0; i < sizeof(gpreg_dict)/sizeof(reg_dict); i++){
	    if (strcmp(PyString_AsString(d_key), gpreg_dict[i].name))
		continue;
	    *((uint32_t*)(((char*)(self->cpu)) + gpreg_dict[i].offset)) = val;
	    found = 1;
	    break;
	}

	if (found)
	    continue;
	fprintf(stderr, "unknown key: %s\n", PyString_AsString(d_key));
	RAISE(PyExc_ValueError, "unknown reg");
    }

    Py_INCREF(Py_None);
    return Py_None;
}


PyObject *
cpu_init_regs(JitCpu *self) {
    memset(self->cpu, 0, sizeof(struct vm_cpu));

    Py_INCREF(Py_None);
    return Py_None;
}

static void
dump_gpreg(const char *name, uint32_t val, int *n) {
    printf("%6s %.8" PRIX32"%c", name, val, (*n + 1) % 4 == 0? '\n':' ');
    *n = (*n + 1) % 4;
}

void
dump_gpregs(struct vm_cpu *vmcpu) {
    int reg_num = 0;

#define JITCORE_PPC_REG_EXPAND(_name, _size) \
    dump_gpreg(#_name, vmcpu->_name, &reg_num);
#include "JitCore_ppc32_regs.h"
#undef JITCORE_PPC_REG_EXPAND

    if ((reg_num % 4) != 0)
      putchar('\n');
}


PyObject *
cpu_dump_gpregs(JitCpu *self, PyObject *args) {

    dump_gpregs(self->cpu);

    Py_INCREF(Py_None);
    return Py_None;
}

PyObject *
cpu_set_exception(JitCpu *self, PyObject *args) {
    PyObject *item1;
    uint64_t i;

    if (!PyArg_ParseTuple(args, "O", &item1))
	return NULL;

    PyGetInt(item1, i);

    ((struct vm_cpu *)self->cpu)->exception_flags = i;

    Py_INCREF(Py_None);
    return Py_None;
}

PyObject *
cpu_get_exception(JitCpu *self, PyObject *args) {
    return PyLong_FromUnsignedLongLong(((struct vm_cpu *)self->cpu)->exception_flags);
}

static PyObject *
cpu_get_spr_access(JitCpu *self, PyObject *args) {
    return PyLong_FromUnsignedLongLong(((struct vm_cpu *) self->cpu)->spr_access);
}

void
check_automod(JitCpu *jitcpu, uint64_t addr, uint64_t size) {
    PyObject *result;

    if (!(((VmMngr*)jitcpu->pyvm)->vm_mngr.exception_flags & EXCEPT_CODE_AUTOMOD))
	return;
    result = PyObject_CallMethod(jitcpu->jitter, "automod_cb", "LL", addr, size);
    Py_DECREF(result);
}

void MEM_WRITE_08(JitCpu* jitcpu, uint64_t addr, uint8_t src)
{
	vm_MEM_WRITE_08(&((VmMngr*)jitcpu->pyvm)->vm_mngr, addr, src);
	check_automod(jitcpu, addr, 8);
}

void MEM_WRITE_16(JitCpu* jitcpu, uint64_t addr, uint16_t src)
{
	vm_MEM_WRITE_16(&((VmMngr*)jitcpu->pyvm)->vm_mngr, addr, src);
	check_automod(jitcpu, addr, 16);
}

void MEM_WRITE_32(JitCpu* jitcpu, uint64_t addr, uint32_t src)
{
	vm_MEM_WRITE_32(&((VmMngr*)jitcpu->pyvm)->vm_mngr, addr, src);
	check_automod(jitcpu, addr, 32);
}

void MEM_WRITE_64(JitCpu* jitcpu, uint64_t addr, uint64_t src)
{
	vm_MEM_WRITE_64(&((VmMngr*)jitcpu->pyvm)->vm_mngr, addr, src);
	check_automod(jitcpu, addr, 64);
}



PyObject *
vm_set_mem(JitCpu *self, PyObject *args) {
   PyObject *py_addr;
   PyObject *py_buffer;
   Py_ssize_t py_length;

   char *buffer;
   uint64_t size;
   uint64_t addr;
   int ret = 0x1337;

   if (!PyArg_ParseTuple(args, "OO", &py_addr, &py_buffer))
       return NULL;

   PyGetInt(py_addr, addr);

   if(!PyString_Check(py_buffer))
       RAISE(PyExc_TypeError,"arg must be str");

   size = PyString_Size(py_buffer);
   PyString_AsStringAndSize(py_buffer, &buffer, &py_length);

   ret = vm_write_mem(&(((VmMngr*)self->pyvm)->vm_mngr), addr, buffer, size);
   if (ret < 0)
       RAISE(PyExc_TypeError,"arg must be str");
   check_automod(self, addr, size*8);

   Py_INCREF(Py_None);
   return Py_None;
}

static PyMemberDef JitCpu_members[] = {
    {NULL}  /* Sentinel */
};

static PyMethodDef JitCpu_methods[] = {
    {"init_regs", (PyCFunction)cpu_init_regs, METH_NOARGS, "X"},
    {"dump_gpregs", (PyCFunction)cpu_dump_gpregs, METH_NOARGS, "X"},
    {"get_gpreg", (PyCFunction)cpu_get_gpreg, METH_NOARGS, "X"},
    {"set_gpreg", (PyCFunction)cpu_set_gpreg, METH_VARARGS, "X"},
    {"get_exception", (PyCFunction)cpu_get_exception, METH_VARARGS, "X"},
    {"set_exception", (PyCFunction)cpu_set_exception, METH_VARARGS, "X"},
    {"get_spr_access", (PyCFunction)cpu_get_spr_access, METH_VARARGS, "X"},
    {"set_mem", (PyCFunction)vm_set_mem, METH_VARARGS, "X"},
    {"get_mem", (PyCFunction)vm_get_mem, METH_VARARGS, "X"},
    {NULL}  /* Sentinel */
};

static int
JitCpu_init(JitCpu *self, PyObject *args, PyObject *kwds) {
    self->cpu = malloc(sizeof(struct vm_cpu));
    if (self->cpu == NULL) {
	fprintf(stderr, "cannot alloc struct vm_cpu\n");
	exit(1);
    }
    return 0;
}


#define JITCORE_PPC_REG_EXPAND(_name, _size) \
getset_reg_u32(_name);
#include "JitCore_ppc32_regs.h"
#undef JITCORE_PPC_REG_EXPAND

PyObject *
get_gpreg_offset_all(void) {
    PyObject *dict = PyDict_New();
    PyObject *o;

#define JITCORE_PPC_REG_EXPAND(_name, _size)				\
    get_reg_off(_name);
#include "JitCore_ppc32_regs.h"
#undef JITCORE_PPC_REG_EXPAND

    return dict;
}

static PyGetSetDef JitCpu_getseters[] = {
    {"vmmngr",
     (getter)JitCpu_get_vmmngr, (setter)JitCpu_set_vmmngr,
     "vmmngr",
     NULL},

    {"jitter",
     (getter)JitCpu_get_jitter, (setter)JitCpu_set_jitter,
     "jitter",
     NULL},

#define JITCORE_PPC_REG_EXPAND(_name, _size)				\
    { #_name, (getter) JitCpu_get_ ## _name ,				\
	(setter) JitCpu_set_ ## _name , #_name , NULL},
#include "JitCore_ppc32_regs.h"
#undef JITCORE_PPC_REG_EXPAND

    {NULL}  /* Sentinel */
};


static PyTypeObject JitCpuType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "JitCore_ppc.JitCpu",      /*tp_name*/
    sizeof(JitCpu),            /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)JitCpu_dealloc,/*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    "JitCpu objects",          /* tp_doc */
    0,			       /* tp_traverse */
    0,			       /* tp_clear */
    0,			       /* tp_richcompare */
    0,			       /* tp_weaklistoffset */
    0,			       /* tp_iter */
    0,			       /* tp_iternext */
    JitCpu_methods,            /* tp_methods */
    JitCpu_members,            /* tp_members */
    JitCpu_getseters,          /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)JitCpu_init,     /* tp_init */
    0,                         /* tp_alloc */
    JitCpu_new,                /* tp_new */
};



static PyMethodDef JitCore_ppc_Methods[] = {
    {"get_gpreg_offset_all", (PyCFunction)get_gpreg_offset_all, METH_NOARGS},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static PyObject *JitCore_ppc32_Error;

PyMODINIT_FUNC
initJitCore_ppc32(void)
{
    PyObject *m;

    if (PyType_Ready(&JitCpuType) < 0)
	return;

    m = Py_InitModule("JitCore_ppc32", JitCore_ppc_Methods);
    if (m == NULL)
	return;

    JitCore_ppc32_Error = PyErr_NewException("JitCore_ppc32.error", NULL, NULL);
    Py_INCREF(JitCore_ppc32_Error);
    PyModule_AddObject(m, "error", JitCore_ppc32_Error);

    Py_INCREF(&JitCpuType);
    PyModule_AddObject(m, "JitCpu", (PyObject *)&JitCpuType);

}

