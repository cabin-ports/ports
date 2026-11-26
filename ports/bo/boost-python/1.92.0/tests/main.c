/* Embeds the system's Python through its C interface, which the package's
 * `python` target supplies: evaluates an expression and reads the result. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdio.h>

int main(void) {
    PyObject *globals, *result;
    long value;

    Py_Initialize();
    globals = PyModule_GetDict(PyImport_AddModule("__main__"));
    result = PyRun_String("sum(range(10))", Py_eval_input, globals, globals);
    if (!result) {
        PyErr_Print();
        return 1;
    }
    value = PyLong_AsLong(result);
    Py_DECREF(result);
    if (Py_FinalizeEx() != 0 || value != 45) {
        fprintf(stderr, "FAIL: sum(range(10)) gave %ld\n", value);
        return 1;
    }
    printf("boost-python: C embedded Python %d.%d and evaluated an expression\n", PY_MAJOR_VERSION,
           PY_MINOR_VERSION);
    return 0;
}
