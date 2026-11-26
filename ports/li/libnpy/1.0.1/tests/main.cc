#include <npy.hpp>

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

template <typename T>
std::string roundtrip(const npy::npy_data<T> &in, npy::npy_data<T> &out) {
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  npy::write_npy(ss, in);
  std::string bytes = ss.str();
  out = npy::read_npy<T>(ss);
  return bytes;
}

}  // namespace

int main() {
  const std::string magic = "\x93NUMPY";

  npy::npy_data<double> d;
  d.data = {1.5, -2.0, 3.25, 4.0, 5.0, 6.125};
  d.shape = {2, 3};
  d.fortran_order = false;
  npy::npy_data<double> dr;
  std::string bytes = roundtrip(d, dr);
  check(bytes.compare(0, 6, magic) == 0, "double magic");
  check(bytes[6] == 1 && bytes[7] == 0, "version 1.0");
  const std::string dict = "{'descr': '<f8', 'fortran_order': False, 'shape': (2, 3), }";
  check(bytes.find(dict) == 10, "double header dict");
  const std::size_t header_end = 10 + static_cast<std::uint8_t>(bytes[8]) + 256u * static_cast<std::uint8_t>(bytes[9]);
  check(header_end % 16 == 0 && bytes[header_end - 1] == '\n', "header padded to 16");
  check(bytes.size() == header_end + 6 * sizeof(double), "payload size");
  check(dr.shape == npy::shape_t({2, 3}), "double shape");
  check(!dr.fortran_order, "double c order");
  check(dr.data == d.data, "double values");

  npy::npy_data<std::int32_t> i;
  i.data = {1, -2, 3, 2147483647};
  i.shape = {4};
  i.fortran_order = true;
  npy::npy_data<std::int32_t> ir;
  bytes = roundtrip(i, ir);
  check(bytes.compare(0, 6, magic) == 0, "int32 magic");
  check(bytes.find("'fortran_order': True, 'shape': (4,)") != std::string::npos, "int32 header");
  check(ir.shape == npy::shape_t({4}), "int32 shape");
  check(ir.fortran_order, "int32 fortran order");
  check(ir.data == i.data, "int32 values");

  bool threw = false;
  try {
    std::stringstream ss(bytes, std::ios::in | std::ios::binary);
    npy::read_npy<double>(ss);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  check(threw, "dtype mismatch throws");

  return failures == 0 ? 0 : 1;
}
