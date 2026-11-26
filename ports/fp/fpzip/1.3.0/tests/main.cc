// The field generator, settings and checksums are adapted from upstream's
// tests/testfpzip.c, under the license below; ldexp(seed, -31) is written as an
// exact division so that the test needs no libm.
//
// BSD 3-Clause License
//
// Copyright (c) 2018-2019, Lawrence Livermore National Security, LLC
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

#include <fpzip.h>

namespace {

const int nx = 65, ny = 64, nz = 63, n = nx * ny * nz;

int failures = 0;

void check(bool cond, const char *what) {
  if (!cond) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

double double_rand() {
  static unsigned int seed = 1;
  seed = 1103515245 * seed + 12345;
  seed &= 0x7fffffffu;
  double val = static_cast<double>(seed) / 2147483648.0;
  val = 2 * val - 1;
  val *= val * val;
  val *= val * val;
  return val;
}

template <typename T> std::vector<T> make_field() {
  std::vector<T> f(n);
  f[0] = 0;
  for (int i = 1; i < n; i++)
    f[i] = static_cast<T>(double_rand());
  for (int z = 0; z < nz; z++)
    for (int y = 0; y < ny; y++)
      for (int x = 1; x < nx; x++)
        f[x + nx * (y + ny * z)] += f[(x - 1) + nx * (y + ny * z)];
  for (int z = 0; z < nz; z++)
    for (int y = 1; y < ny; y++)
      for (int x = 0; x < nx; x++)
        f[x + nx * (y + ny * z)] += f[x + nx * ((y - 1) + ny * z)];
  for (int z = 1; z < nz; z++)
    for (int y = 0; y < ny; y++)
      for (int x = 0; x < nx; x++)
        f[x + nx * (y + ny * z)] += f[x + nx * (y + ny * (z - 1))];
  return f;
}

// Jenkins one-at-a-time hash, as upstream's test.
unsigned int checksum(const unsigned char *p, std::size_t size) {
  unsigned int h = 0;
  for (; size; p++, size--) {
    h += *p;
    h += h << 10;
    h ^= h >> 6;
  }
  h += h << 3;
  h ^= h >> 11;
  h += h << 15;
  return h;
}

// Compresses field at prec, checks the stream hash (when expected is
// nonzero) and the header read back, and decompresses into copy.
template <typename T>
void round_trip(int type, const std::vector<T> &field, std::vector<T> &copy,
                int prec, unsigned int expected) {
  std::size_t bufbytes = 1024 + n * sizeof(T);
  std::vector<unsigned char> buffer(bufbytes);
  FPZ *fpz = fpzip_write_to_buffer(buffer.data(), bufbytes);
  fpz->type = type;
  fpz->prec = prec;
  fpz->nx = nx;
  fpz->ny = ny;
  fpz->nz = nz;
  fpz->nf = 1;
  check(fpzip_write_header(fpz) != 0, "write header");
  std::size_t outbytes = fpzip_write(fpz, field.data());
  check(outbytes > 0 && outbytes <= bufbytes, "compress");
  fpzip_write_close(fpz);
  if (expected)
    check(checksum(buffer.data(), outbytes) == expected, "stream checksum");

  fpz = fpzip_read_from_buffer(buffer.data());
  check(fpzip_read_header(fpz) != 0, "read header");
  check(fpz->type == type && fpz->nx == nx && fpz->ny == ny && fpz->nz == nz &&
            fpz->nf == 1 && (prec == 0 || fpz->prec == prec),
        "header read back");
  check(fpzip_read(fpz, copy.data()) > 0, "decompress");
  fpzip_read_close(fpz);
}

template <typename T>
void test_type(int type, const int (&precs)[3], const unsigned int (&sums)[3],
               int lossy_prec, T rel_bound) {
  const std::vector<T> field = make_field<T>();
  std::vector<T> copy(n);
  for (int i = 0; i < 3; i++)
    round_trip(type, field, copy, precs[i], sums[i]);
  check(field == copy, "full precision is lossless");

  round_trip(type, field, copy, 0, 0);
  check(field == copy, "prec 0 is lossless");

  round_trip(type, field, copy, lossy_prec, 0);
  bool within = true;
  for (int i = 0; i < n; i++) {
    T d = copy[i] - field[i];
    T a = field[i] < 0 ? -field[i] : field[i];
    if (d < 0)
      d = -d;
    within = within && d <= a * rel_bound;
  }
  check(within, "lossy error bound");
}

} // namespace

int main() {
  check(fpzip_library_version == 0x130u, "library version");
  check(std::strcmp(fpzip_version_string,
                    "fpzip version 1.3.0 (December 20, 2019)") == 0,
        "version string");
  check(fpzip_codec_version == 0x11001u, "codec version");

  // prec 16 keeps 7 float mantissa bits, prec 32 keeps 20 double ones.
  const int fprecs[3] = {8, 16, 32};
  const unsigned int fsums[3] = {0x8b8996d3u, 0xb17f5c48u, 0x2cb98d38u};
  test_type<float>(FPZIP_TYPE_FLOAT, fprecs, fsums, 16, 1.0f / 128);
  const int dprecs[3] = {16, 32, 64};
  const unsigned int dsums[3] = {0x1582bfc9u, 0x4e9fa29du, 0x0dd20faau};
  test_type<double>(FPZIP_TYPE_DOUBLE, dprecs, dsums, 32, 1.0 / 1048576);

  unsigned char zeros[64] = {};
  FPZ *fpz = fpzip_read_from_buffer(zeros);
  check(fpzip_read_header(fpz) == 0, "header of a non-fpzip stream is refused");
  check(fpzip_errno == fpzipErrorBadFormat, "bad format error code");
  fpzip_read_close(fpz);

  if (failures)
    return 1;
  std::puts("ok");
  return 0;
}
