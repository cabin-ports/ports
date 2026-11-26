// Embeds the system's Python and imports a module this program defines with
// Boost.Python: Python calls a wrapped function and class, a C++ exception
// reaches Python as the exception Boost.Python maps it to, and C++ calls
// back into Python and reads the results.
#include <boost/python.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace python = boost::python;

namespace {

struct Counter {
  int total = 0;
  void add(int n) { total += n; }
};

int twice(int x) { return 2 * x; }

void refuse(const std::string& what) { throw std::invalid_argument(what); }

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

}  // namespace

BOOST_PYTHON_MODULE(cabin_probe) {
  python::def("twice", twice);
  python::def("refuse", refuse);
  python::class_<Counter>("Counter").def("add", &Counter::add).def_readonly("total", &Counter::total);
}

int main() {
  if (PyImport_AppendInittab("cabin_probe", PyInit_cabin_probe) == -1) return 1;
  Py_Initialize();
  try {
    python::object globals = python::import("__main__").attr("__dict__");
    python::exec(
        "import sys, cabin_probe\n"
        "counter = cabin_probe.Counter()\n"
        "for i in range(10):\n"
        "    counter.add(cabin_probe.twice(i))\n"
        "try:\n"
        "    cabin_probe.refuse('no')\n"
        "    refused = None\n"
        "except ValueError as error:\n"
        "    refused = str(error)\n"
        "version = '%d.%d.%d' % sys.version_info[:3]\n",
        globals, globals);
    check(python::extract<int>(globals["counter"].attr("total"))() == 90, "Counter.total");
    check(python::extract<std::string>(globals["refused"])() == "no",
          "std::invalid_argument reaches Python as ValueError");

    python::object sorted = python::import("builtins").attr("sorted");
    python::list values = python::extract<python::list>(sorted(python::make_tuple(3, 1, 2)));
    check(python::len(values) == 3 && python::extract<int>(values[0])() == 1 &&
              python::extract<int>(values[2])() == 3,
          "C++ calls sorted()");

    if (!ok) return 1;
    const std::string version = python::extract<std::string>(globals["version"]);
    std::printf("boost-python: Python %s ran a module defined in C++\n", version.c_str());
    return 0;
  } catch (const python::error_already_set&) {
    PyErr_Print();
    return 1;
  }
}
