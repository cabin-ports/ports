// Embeds the system's Python and calls a C++ member function that
// Boost.Parameter's Python binding exposes: by position, by keyword in any
// order, and with an optional parameter left to its default.
#include <boost/mpl/vector.hpp>
#include <boost/parameter/keyword.hpp>
#include <boost/parameter/preprocessor.hpp>
#include <boost/parameter/python.hpp>
#include <boost/python.hpp>

#include <cstdio>

namespace python = boost::python;

namespace probe {

BOOST_PARAMETER_KEYWORD(tags, width)
BOOST_PARAMETER_KEYWORD(tags, height)
BOOST_PARAMETER_KEYWORD(tags, scale)

struct Box {
  BOOST_PARAMETER_BASIC_MEMBER_FUNCTION((int), area, tags,
                                        (required(width, *)(height, *))(optional(scale, *))) {
    return args[width] * args[height] * args[scale | 1];
  }
};

struct area_forwarder {
  template <class R, class T, class A0, class A1, class A2>
  R operator()(boost::type<R>, T& self, const A0& a0, const A1& a1, const A2& a2) {
    return self.area(a0, a1, a2);
  }
};

}  // namespace probe

BOOST_PYTHON_MODULE(cabin_probe) {
  python::class_<probe::Box>("Box").def(
      "area",
      boost::parameter::python::function<
          probe::area_forwarder,
          boost::mpl::vector<int, probe::tags::width(int), probe::tags::height(int),
                             probe::tags::scale*(int)>>());
}

int main() {
  if (PyImport_AppendInittab("cabin_probe", PyInit_cabin_probe) == -1) return 1;
  Py_Initialize();
  try {
    python::object globals = python::import("__main__").attr("__dict__");
    python::exec(
        "import cabin_probe\n"
        "box = cabin_probe.Box()\n"
        "areas = [box.area(2, 3), box.area(2, 3, 4), box.area(2, 3, scale=5),\n"
        "         box.area(scale=2, height=3, width=4)]\n"
        "try:\n"
        "    box.area(2)\n"
        "    refused = False\n"
        "except TypeError:\n"
        "    refused = True\n",
        globals, globals);
    const int want[] = {6, 24, 30, 24};
    bool ok = python::len(globals["areas"]) == 4;
    for (int i = 0; ok && i < 4; ++i) {
      ok = python::extract<int>(globals["areas"][i])() == want[i];
    }
    if (!ok) std::fprintf(stderr, "FAIL: areas by position and by keyword\n");
    if (!python::extract<bool>(globals["refused"])()) {
      std::fprintf(stderr, "FAIL: a call without a required parameter is a TypeError\n");
      ok = false;
    }
    if (!ok) return 1;
    std::puts("boost-parameter-python: Python called a C++ function by keyword");
    return 0;
  } catch (const python::error_already_set&) {
    PyErr_Print();
    return 1;
  }
}
