// Checks what Boost.Compute does on the host, which needs no OpenCL library
// or device: the OpenCL C type names it writes, the definition it writes for
// an adapted struct, the source of user functions, the host vector types, and
// its error strings.  The kernel probe package runs that code on a device.
#include <boost/compute/exception/opencl_error.hpp>
#include <boost/compute/function.hpp>
#include <boost/compute/type_traits/type_definition.hpp>
#include <boost/compute/type_traits/type_name.hpp>
#include <boost/compute/types/fundamental.hpp>
#include <boost/compute/types/struct.hpp>

#include <cstdio>
#include <sstream>
#include <string>

namespace compute = boost::compute;

struct Particle {
  compute::float2_ position;
  compute::float2_ velocity;
};

BOOST_COMPUTE_ADAPT_STRUCT(Particle, Particle, (position, velocity))

BOOST_COMPUTE_FUNCTION(int, add_four, (int x), { return x + 4; });
BOOST_COMPUTE_FUNCTION(float, scale, (float x, int n), { return x * n; });

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

void check_equal(const std::string& got, const std::string& want, const char* what) {
  if (got != want) {
    std::fprintf(stderr, "FAIL: %s: got \"%s\", want \"%s\"\n", what, got.c_str(), want.c_str());
    ok = false;
  }
}

}  // namespace

int main() {
  check_equal(compute::type_name<compute::int_>(), "int", "type_name<int_>");
  check_equal(compute::type_name<compute::ulong_>(), "ulong", "type_name<ulong_>");
  check_equal(compute::type_name<compute::float4_>(), "float4", "type_name<float4_>");
  check_equal(compute::type_name<compute::char16_>(), "char16", "type_name<char16_>");
  check_equal(compute::type_name<Particle>(), "Particle", "type_name<Particle>");

  check_equal(compute::type_definition<Particle>(),
              "typedef struct __attribute__((packed)) {\n"
              "    float2 position;\n"
              "    float2 velocity;\n"
              "} Particle;\n",
              "type_definition<Particle>");

  check_equal(add_four.name(), "add_four", "add_four.name()");
  check_equal(add_four.source(), "inline int add_four(int x){ return x + 4; }",
              "add_four.source()");
  check_equal(scale.source(), "inline float scale(float x, int n){ return x * n; }",
              "scale.source()");

  compute::int4_ v(1, 2, 3, 4);
  check(v[2] == 3, "int4_ element");
  check(v == compute::int4_(1, 2, 3, 4), "int4_ ==");
  check(v != compute::int4_(1, 2, 3, 5), "int4_ !=");
  check(compute::float2_(2.5f) == compute::float2_(2.5f, 2.5f), "float2_ broadcast");
  std::ostringstream printed;
  printed << v;
  check_equal(printed.str(), "int4(1, 2, 3, 4)", "int4_ <<");

  check_equal(compute::opencl_error::to_string(CL_SUCCESS), "Success", "to_string(CL_SUCCESS)");
  check_equal(compute::opencl_error::to_string(CL_INVALID_VALUE), "Invalid Value",
              "to_string(CL_INVALID_VALUE)");
  compute::opencl_error error(CL_OUT_OF_HOST_MEMORY);
  check(error.error_code() == CL_OUT_OF_HOST_MEMORY, "opencl_error::error_code");
  check_equal(error.what(), "Out of Host Memory", "opencl_error::what");

  if (!ok) return 1;
  std::puts("boost-compute: host-side checks passed");
  return 0;
}
