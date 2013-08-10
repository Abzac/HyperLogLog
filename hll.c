#include <Python.h>
#include "structmember.h"
#include "hll.h"
#include "murmur3.h"
#include <math.h>
#include <stdint.h>

typedef struct {
    PyObject_HEAD
    short int k;      /* power, size = 2^k */
    uint32_t seed;    /* Murmur3 Hash seed value */
    uint32_t size;    /* number of registers */
    char * registers; /* array of ranks */
} HyperLogLog;

static void
HyperLogLog_dealloc(HyperLogLog* self)
{
    free(self->registers);
#if PY_MAJOR_VERSION >= 3
    Py_TYPE(self)->tp_free((PyObject*)self);
#else
    self->ob_type->tp_free((PyObject*)self);
#endif
}

static PyObject *
HyperLogLog_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    HyperLogLog *self;
    self = (HyperLogLog *)type->tp_alloc(type, 0);
    self->seed = 314;
    return (PyObject *)self;
}

static int
HyperLogLog_init(HyperLogLog *self, PyObject *args, PyObject *kwds)
{ 
    static char *kwlist[] = {"k", "seed", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|i", kwlist, 
				      &self->k, &self->seed)) {
        return -1; 
    }

    if (self->k < 2 || self->k > 16) {
        char * msg = "Number of registers must be in the range [2^2, 2^16]";
        PyErr_SetString(PyExc_ValueError, msg);
	return -1;
    } 

    self->size = 1 << self->k;
    self->registers = (char *)malloc(self->size * sizeof(char));
    memset(self->registers, 0, self->size);
    return 0; 
}

/*
 * Instance members of type HyperLogLog.
 */
static PyMemberDef HyperLogLog_members[] = { 
    {NULL} /* Sentinel */
};

/*
 * Adds an element to the cardinality estimator.
 */
static PyObject *
HyperLogLog_add(HyperLogLog *self, PyObject *args)
{
    const char *data;
    const uint32_t dataLength;

    if (!PyArg_ParseTuple(args, "s#", &data, &dataLength))
        return NULL;

    uint32_t *hash = (uint32_t *) malloc(sizeof(uint32_t));
    uint32_t index;
    uint32_t rank;

    MurmurHash3_x86_32((void *) data, dataLength, self->seed, (void *) hash);

    /* use the first k bits as an index */
    index = (*hash >> (32 - self->k)) + 1;

    /* compute the rank of the remaining 32 - k bits */
    rank = leadingZeroCount((*hash << self->k) >> self->k) - self->k + 1;
    
    if (rank > self->registers[index])
        self->registers[index] = rank;

    Py_INCREF(Py_None);
    return Py_None;
};

/*
 * Gets the cardinality estimate.
 */
static PyObject *
HyperLogLog_cardinality(HyperLogLog *self)
{
    static const double two_32 = 4294967296.0;
    static const double neg_two_32 = -4294967296.0;

    double alpha = 0.0;
    switch (self->size) {
        case 16:
      	  alpha = 0.673;
	  break;
        case 32:
	  alpha = 0.697;
	  break;
        case 64:
	  alpha = 0.709;
	  break;
        default:
	  alpha = 0.7213/(1.0 + 1.079/(double) self->size);
          break;
    }
  
    uint32_t i;
    uint32_t rank;
    double sum = 0.0;
    for (i = 0; i < self->size; i++) {
        rank = self->registers[i];
        sum = sum + 1.0/pow(2, rank);
    }

    double raw_estimate = alpha * (1/sum) * self->size * self->size;   
    double estimate = 0;   
    if (raw_estimate <= 2.5 * self->size) {
        uint32_t zeros = 0;
	uint32_t i;

	for (i = 0; i < self->size; i++) {
    	    if (self->registers == 0)
	        zeros += 1;
	}

        if (zeros != 0)
            estimate = self->size * log(self->size/zeros);
	else
            estimate = raw_estimate;
    }
    
    if (estimate <= (1.0/30.0) * two_32)
        estimate = raw_estimate;
    
    if (estimate > (1.0/30.0) * two_32)
        estimate = neg_two_32 * log(1.0 - raw_estimate/two_32);

    return Py_BuildValue("d", estimate);
}

/*
 * Get a Murmur3 hash of |data| as an unsigned integer.
 */
static PyObject *
HyperLogLog_murmur3_hash(HyperLogLog *self, PyObject *args)
{
    const char *data;
    const uint32_t dataLength;

    if (!PyArg_ParseTuple(args, "s#", &data, &dataLength))
        return NULL;

    uint32_t *hash = (uint32_t *) malloc(sizeof(uint32_t));
    MurmurHash3_x86_32((void *) data, dataLength, self->seed, (void *) hash);
    return Py_BuildValue("i", *hash);
}


/*
 * Merges another HyperLogLog with the current HyperLogLog by taking the maximum
 * value of each register. The registers of the other HyperLogLog are 
 * unaffected.
 */
static PyObject *
HyperLogLog_merge(HyperLogLog *self, PyObject * args) 
{
  
    PyObject *hll;
    if (!PyArg_ParseTuple(args, "O", &hll)) //TODO: use O! to check type
        return NULL;

    PyObject *size = PyObject_CallMethod(hll, "size", NULL);

#if PY_MAJOR_VERSION >= 3
    long hllSize = PyLong_AsLong(size);
#else
    long hllSize = PyInt_AS_LONG(size);
#endif

    if (hllSize != self->size) {
        PyErr_SetString(PyExc_ValueError, "HyperLogLogs must be the same size");
        return NULL;
    }

    PyObject *hllByteArray = PyObject_CallMethod(hll, "registers", NULL);
    char *hllRegisters = PyByteArray_AsString(hllByteArray);

    uint32_t i;
    for (i = 0; i < self->size; i++) {
        if (self->registers[i] < hllRegisters[i])
	    self->registers[i] = hllRegisters[i];
    }

    free(hllRegisters);
    Py_INCREF(Py_None);
    return Py_None;
} 


/*
 * Gets a copy of the registers as a bytesarray.
 */
static PyObject *
HyperLogLog_registers(HyperLogLog *self)
{
    PyObject* registers;
    registers = PyByteArray_FromStringAndSize(self->registers, self->size);
    return registers;
}

/*
 * Sets register |index| to |rank|.
 */
static PyObject *
HyperLogLog_set_register(HyperLogLog *self, PyObject * args)
{
    const uint32_t index;
    const uint32_t rank;

    if (!PyArg_ParseTuple(args, "ii", &index, &rank))
        return NULL;

    if (index < 0) {
        char * msg = "Index is negative.";
        PyErr_SetString(PyExc_ValueError, msg);
        return NULL;
    }

    if (index > self->size) {
        char * msg = "Index greater than the number of registers.";
        PyErr_SetString(PyExc_IndexError, msg);
        return NULL;
    }

    if (rank > 32) {
        char * msg = "Rank is greater than the maximum possible rank.";
        PyErr_SetString(PyExc_ValueError, msg);
        return NULL;
    }

    if (rank < 0) {
        char * msg = "Rank is negative.";
        PyErr_SetString(PyExc_ValueError, msg);
        return NULL;
    }

    self->registers[index] = rank;

    Py_INCREF(Py_None);
    return Py_None;

}

/*
 * Gets the seed.
 */
static PyObject *
HyperLogLog_seed(HyperLogLog* self)
{
    return Py_BuildValue("i", self->seed);
}

/*
 * Gets the number of registers.
 */
static PyObject *
HyperLogLog_size(HyperLogLog* self)
{
    return Py_BuildValue("i", self->size);
}

static PyMethodDef HyperLogLog_methods[] = {
    {"add", (PyCFunction)HyperLogLog_add, METH_VARARGS,
     "Add an element to a random register."
    },
    {"cardinality", (PyCFunction)HyperLogLog_cardinality, METH_NOARGS,
     "Get the cardinality."
    },
    {"merge", (PyCFunction)HyperLogLog_merge, METH_VARARGS,
     "Merge another HyperLogLog object with the current HyperLogLog."
     },
    {"murmur3_hash", (PyCFunction)HyperLogLog_murmur3_hash, METH_VARARGS,
     "Gets a Murmur3 hash of the passed data."
     },
    {"registers", (PyCFunction)HyperLogLog_registers, METH_NOARGS, 
     "Get a copy of the registers as a bytearray."
     },
    {"seed", (PyCFunction)HyperLogLog_seed, METH_NOARGS, 
     "Get the seed used in the Murmur3 hash."
     },
    {"set_register", (PyCFunction)HyperLogLog_set_register, METH_VARARGS, 
     "Set the register at a zero-based index to the specified rank." 
     },
    {"size", (PyCFunction)HyperLogLog_size, METH_NOARGS, 
     "Returns the number of registers."
    },
    {NULL}  /* Sentinel */
};

static PyTypeObject HyperLogLogType = {
#if PY_MAJOR_VERSION >= 3
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
#endif
    "HLL.HyperLogLog",         /*tp_name*/
    sizeof(HyperLogLog),       /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)HyperLogLog_dealloc, /*tp_dealloc*/
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
    Py_TPFLAGS_DEFAULT | 
        Py_TPFLAGS_BASETYPE, /*tp_flags*/
    "HyperLogLog object",      /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    HyperLogLog_methods,       /* tp_methods */
    HyperLogLog_members,       /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)HyperLogLog_init,      /* tp_init */
    0,                         /* tp_alloc */
    HyperLogLog_new,           /* tp_new */
};

#if PY_MAJOR_VERSION >= 3
static PyModuleDef HyperLogLogmodule = {
    PyModuleDef_HEAD_INIT,
    "HyperLogLog",
    "A space efficient cardinality estimator.",
    -1,
    NULL, NULL, NULL, NULL, NULL
};

#else
static PyMethodDef module_methods[] = {
    {NULL}  /* Sentinel */
};
#endif

#if PY_MAJOR_VERSION >=3
PyMODINIT_FUNC
PyInit_HLL(void)
#else
#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC initHLL(void) 
#endif
{
    PyObject* m;
    if (PyType_Ready(&HyperLogLogType) < 0) {

#if PY_MAJOR_VERSION >= 3
        return NULL;
    }

    m = PyModule_Create(&HyperLogLogmodule);
#else
        return;
    }

    char *description = "HyperLogLog cardinality estimator.";
    m = Py_InitModule3("HLL", module_methods, description);
#endif

    if (m == NULL)
#if PY_MAJOR_VERSION >= 3
        return NULL;
#else
        return;
#endif

    Py_INCREF(&HyperLogLogType);
    PyModule_AddObject(m, "HyperLogLog", (PyObject *)&HyperLogLogType);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}


/* 
 * Get the number of leading zeros in |x|.
 */
uint32_t leadingZeroCount(uint32_t x) {
  x |= (x >> 1);
  x |= (x >> 2);
  x |= (x >> 4);
  x |= (x >> 8);
  x |= (x >> 16);
  return (32 - ones(x));
}

/*
 * Get the number of bits set to 1 in |x|.
 */
uint32_t ones(uint32_t x) {
  x -= (x >> 1) & 0x55555555;
  x = ((x >> 2) & 0x33333333) + (x & 0x33333333);
  x = ((x >> 4) + x) & 0x0F0F0F0F;
  x += (x >> 8);
  x += (x >> 16);
  return(x & 0x0000003F);
}
