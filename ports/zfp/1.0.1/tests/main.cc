// The C++ consumer: zfp's compressed arrays built from upstream's small
// test fields and held to the construction errors and differencing results
// upstream's test expects (tests/testzfp.cpp:595-632, 701-747, 964-1005);
// fixed-rate storage; serialization through a header's bytes and
// zfp::array::construct; mismatched headers and short buffers thrown as
// zfp::exception; a read-only fixed-accuracy const_array3; and zfp.hpp's
// templated block codec.
#include <zfp.hpp>
#include <zfp/array2.hpp>
#include <zfp/array3.hpp>
#include <zfp/constarray3.hpp>
#include <zfp/factory.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what)
{
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

// testzfp.cpp's refinement and gen_array_2d and gen_array_3d (:43-236) for
// 2^12 scalars: 64^2 or 16^3.
template <typename Scalar>
std::vector<Scalar> generate(unsigned dims, size_t edge)
{
  static const int weight[4] = { -1, 9, 9, -1 };
  std::vector<int> f(4096, 0), g(4096);
  for (size_t i = 0; i < (size_t(1) << (2 * dims)); i++) {
    size_t parity = 0;
    bool inner = true;
    for (unsigned d = 0; d < dims; d++) {
      size_t x = (i >> (2 * d)) & 3u;
      parity ^= x;
      inner = inner && (x == 1 || x == 2);
    }
    f[i] = inner ? 0x10000 * (1 - 2 * int(parity & 1u)) : 0;
  }
  for (size_t m = 4; m < edge; m *= 2) {
    size_t n = 2 * m, count = 1;
    for (unsigned d = 0; d < dims; d++)
      count *= n;
    for (size_t t = 0; t < count; t++) {
      int s = 0;
      for (size_t q = 0; q < (size_t(1) << (2 * dims)); q++) {
        int w = 1;
        size_t index = 0, stride = 1, rest = t;
        for (unsigned d = 0; d < dims; d++) {
          size_t c = rest % n, i = (q >> (2 * d)) & 3u;
          size_t cc = c & 1u ? (c / 2 + i - 1 + m) % m : c / 2;
          rest /= n;
          w *= weight[i];
          index += cc * stride;
          stride *= m;
        }
        s += w * f[index];
      }
      g[t] = s / (1 << (4 * dims));
    }
    f.swap(g);
  }
  std::vector<Scalar> out(f.size());
  for (size_t i = 0; i < f.size(); i++)
    out[i] = std::ldexp(Scalar(f[i]), -12);
  return out;
}

// testzfp.cpp's update_array2 and update_array3: differencing in place.
template <typename Scalar>
void update(zfp::array2<Scalar>& a)
{
  for (size_t j = 0; j < a.size_y(); j++)
    for (size_t i = 0; i < a.size_x() - 1; i++)
      a(i, j) -= a(i + 1, j);
  for (size_t j = 0; j < a.size_y() - 1; j++)
    for (size_t i = 0; i < a.size_x(); i++)
      a(i, j) -= a(i, j + 1);
  for (size_t j = 0; j < a.size_y() - 1; j++)
    for (size_t i = 0; i < a.size_x() - 1; i++)
      a(0, 0) = std::max(a(0, 0), a(i, j));
}

template <typename Scalar>
void update(zfp::array3<Scalar>& a)
{
  for (size_t k = 0; k < a.size_z(); k++)
    for (size_t j = 0; j < a.size_y(); j++)
      for (size_t i = 0; i < a.size_x() - 1; i++)
        a(i, j, k) -= a(i + 1, j, k);
  for (size_t k = 0; k < a.size_z(); k++)
    for (size_t j = 0; j < a.size_y() - 1; j++)
      for (size_t i = 0; i < a.size_x(); i++)
        a(i, j, k) -= a(i, j + 1, k);
  for (size_t k = 0; k < a.size_z() - 1; k++)
    for (size_t j = 0; j < a.size_y(); j++)
      for (size_t i = 0; i < a.size_x(); i++)
        a(i, j, k) -= a(i, j, k + 1);
  for (size_t k = 0; k < a.size_z() - 1; k++)
    for (size_t j = 0; j < a.size_y() - 1; j++)
      for (size_t i = 0; i < a.size_x() - 1; i++)
        a(0, 0, 0) = std::max(a(0, 0, 0), a(i, j, k));
}

// testzfp.cpp's test_array at rate 16 with its small-array expectations.
template <class Array, typename Scalar>
void test_array(Array& a, const std::vector<Scalar>& f, double emax, double dfmax, const char* what)
{
  Scalar e = 0;
  for (size_t i = 0; i < f.size(); i++)
    e = std::max(e, Scalar(std::abs(f[i] - a[i])));
  check(e <= static_cast<Scalar>(emax), what);
  update(a);
  Scalar amax = a[0];
  check(std::abs(amax - static_cast<Scalar>(dfmax)) <= 1e-3 * static_cast<Scalar>(dfmax), what);
}

}  // namespace

int main()
{
  std::vector<float> f2 = generate<float>(2, 64);
  std::vector<double> f3 = generate<double>(3, 16);

  zfp::array2f a2(64, 64, 16, f2.data());
  test_array(a2, f2, 7.630e-06, 3.755e-01, "array2f construct and update");
  zfp::array3d a3(16, 16, 16, 16, f3.data());
  test_array(a3, f3, 3.338e-05, 1.846e+00, "array3d construct and update");

  // Fixed rate: 64 blocks of 4^3 values at 16 bits each.
  zfp::array3d a(16, 16, 16, 16, f3.data());
  check(a.rate() == 16 && a.compressed_size() == 64 * 64 * 16 / 8, "array3d storage");
  a(1, 2, 3) = 42;
  check(a(1, 2, 3) == 42, "array3d cached write");
  // Iterators visit every element once, block by block.
  std::vector<bool> seen(a.size());
  size_t visited = 0;
  bool in_place = true;
  for (zfp::array3d::iterator it = a.begin(); it != a.end(); ++it, ++visited) {
    size_t index = it.i() + 16 * (it.j() + 16 * it.k());
    in_place = in_place && !seen[index] && double(*it) == double(a(it.i(), it.j(), it.k()));
    seen[index] = true;
    if (visited < 64)
      in_place = in_place && it.i() < 4 && it.j() < 4 && it.k() < 4;
  }
  check(visited == a.size() && in_place, "array3d iteration");

  // Serialize the header's bytes and the compressed data, then rebuild.
  zfp::array3d::header header(a);
  const unsigned char* bytes = static_cast<const unsigned char*>(header.data());
  std::vector<unsigned char> saved(bytes, bytes + header.size_bytes());
  std::vector<unsigned char> data(static_cast<const unsigned char*>(a.compressed_data()),
                                  static_cast<const unsigned char*>(a.compressed_data()) + a.compressed_size());
  zfp::array3d::header read(saved.data(), saved.size());
  // The base type selects the header constructors over the view templates.
  const zfp::array::header& base = read;
  std::unique_ptr<zfp::array> built(zfp::array::construct(read, data.data(), data.size()));
  zfp::array3d* b = dynamic_cast<zfp::array3d*>(built.get());
  check(b != nullptr && b->size_x() == 16 && b->size_z() == 16 && b->rate() == 16, "construct from header");
  bool same = b != nullptr;
  for (size_t i = 0; same && i < a.size(); i++)
    same = (*b)[i] == a[i];
  check(same && std::abs((*b)(1, 2, 3) - 42) < 1e-2, "construct round trip");

  bool threw = false;
  try {
    zfp::array2f wrong(base);
  } catch (const zfp::exception&) {
    threw = true;
  }
  check(threw, "mismatched header refused");
  threw = false;
  try {
    zfp::array3d short_buffer(base, data.data(), 8);
  } catch (const zfp::exception&) {
    threw = true;
  }
  check(threw, "short buffer refused");

  // Fixed accuracy, read-only.
  zfp::const_array3<double> c(16, 16, 16, zfp_config_accuracy(1e-6), f3.data());
  double tolerance = c.accuracy(), e = 0;
  for (size_t k = 0; k < 16; k++)
    for (size_t j = 0; j < 16; j++)
      for (size_t i = 0; i < 16; i++)
        e = std::max(e, std::abs(f3[i + 16 * (j + 16 * k)] - double(c(i, j, k))));
  check(tolerance > 0 && tolerance <= 1e-6 && e <= tolerance, "const_array3 accuracy");
  check(c.compressed_size() < f3.size() * sizeof(double), "const_array3 compresses");

  // zfp.hpp: a reversible 3D block round trip through the templates.
  float block[64], decoded[64];
  for (int i = 0; i < 64; i++)
    block[i] = std::sin(float(i)) * 1000;
  std::vector<unsigned char> buffer(4096);
  bitstream* stream = stream_open(buffer.data(), buffer.size());
  zfp_stream* zfp = zfp_stream_open(stream);
  zfp_stream_set_reversible(zfp);
  size_t bits = zfp::encode_block<float, 3>(zfp, block);
  zfp_stream_flush(zfp);
  zfp_stream_rewind(zfp);
  check(bits > 0 && zfp::decode_block<float, 3>(zfp, decoded) == bits, "block bits");
  check(std::memcmp(block, decoded, sizeof block) == 0, "block lossless");
  zfp_stream_close(zfp);
  stream_close(stream);

  if (!ok)
    return EXIT_FAILURE;
  std::puts("zfp C++: ok");
  return EXIT_SUCCESS;
}
