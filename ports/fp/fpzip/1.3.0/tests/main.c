/* The field generator, settings and checksums are adapted from upstream's
   tests/testfpzip.c, under the license below; ldexp(seed, -31) is written as an
   exact division so that the test needs no libm.

   BSD 3-Clause License

   Copyright (c) 2018-2019, Lawrence Livermore National Security, LLC
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

   * Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

   * Neither the name of the copyright holder nor the names of its
     contributors may be used to endorse or promote products derived from
     this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fpzip.h>

enum { nx = 65, ny = 64, nz = 63, n = nx * ny * nz };

static int failures = 0;

static void check(int cond, const char *what) {
  if (!cond) {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

static double double_rand(void) {
  static unsigned int seed = 1;
  double val;
  seed = 1103515245 * seed + 12345;
  seed &= 0x7fffffffu;
  val = (double)seed / 2147483648.0;
  val = 2 * val - 1;
  val *= val * val;
  val *= val * val;
  return val;
}

#define DEFINE_FIELD(NAME, T)                                                 \
  static T *NAME(void) {                                                      \
    T *field = (T *)malloc(n * sizeof(T));                                    \
    int i, x, y, z;                                                           \
    field[0] = 0;                                                             \
    for (i = 1; i < n; i++)                                                   \
      field[i] = (T)double_rand();                                            \
    for (z = 0; z < nz; z++)                                                  \
      for (y = 0; y < ny; y++)                                                \
        for (x = 1; x < nx; x++)                                              \
          field[x + nx * (y + ny * z)] += field[(x - 1) + nx * (y + ny * z)]; \
    for (z = 0; z < nz; z++)                                                  \
      for (y = 1; y < ny; y++)                                                \
        for (x = 0; x < nx; x++)                                              \
          field[x + nx * (y + ny * z)] += field[x + nx * ((y - 1) + ny * z)]; \
    for (z = 1; z < nz; z++)                                                  \
      for (y = 0; y < ny; y++)                                                \
        for (x = 0; x < nx; x++)                                              \
          field[x + nx * (y + ny * z)] += field[x + nx * (y + ny * (z - 1))]; \
    return field;                                                             \
  }

DEFINE_FIELD(float_field, float)
DEFINE_FIELD(double_field, double)

/* Jenkins one-at-a-time hash, as upstream's test. */
static unsigned int checksum(const void *buffer, size_t size) {
  unsigned int h = 0;
  const unsigned char *p;
  for (p = (const unsigned char *)buffer; size; p++, size--) {
    h += *p;
    h += h << 10;
    h ^= h >> 6;
  }
  h += h << 3;
  h ^= h >> 11;
  h += h << 15;
  return h;
}

/* Compresses field at prec, checks the stream hash and the header read
   back, and decompresses into copy.  Returns the compressed size. */
static size_t round_trip(int type, size_t elem, const void *field, void *copy,
                         int prec, unsigned int expected) {
  size_t inbytes = n * elem;
  size_t bufbytes = 1024 + inbytes;
  void *buffer = malloc(bufbytes);
  FPZ *fpz = fpzip_write_to_buffer(buffer, bufbytes);
  size_t outbytes;

  fpz->type = type;
  fpz->prec = prec;
  fpz->nx = nx;
  fpz->ny = ny;
  fpz->nz = nz;
  fpz->nf = 1;
  check(fpzip_write_header(fpz) != 0, "write header");
  outbytes = fpzip_write(fpz, field);
  check(outbytes > 0 && outbytes <= bufbytes, "compress");
  fpzip_write_close(fpz);
  if (expected)
    check(checksum(buffer, outbytes) == expected, "stream checksum");

  fpz = fpzip_read_from_buffer(buffer);
  check(fpzip_read_header(fpz) != 0, "read header");
  check(fpz->type == type && fpz->nx == nx && fpz->ny == ny && fpz->nz == nz &&
            fpz->nf == 1 && (prec == 0 || fpz->prec == prec),
        "header read back");
  check(fpzip_read(fpz, copy) > 0, "decompress");
  fpzip_read_close(fpz);
  free(buffer);
  return outbytes;
}

static void test_float(void) {
  static const int precs[3] = {8, 16, 32};
  static const unsigned int sums[3] = {0x8b8996d3u, 0xb17f5c48u, 0x2cb98d38u};
  float *field = float_field();
  float *copy = (float *)malloc(n * sizeof(float));
  int i;

  for (i = 0; i < 3; i++)
    round_trip(FPZIP_TYPE_FLOAT, sizeof(float), field, copy, precs[i], sums[i]);
  check(memcmp(field, copy, n * sizeof(float)) == 0, "float prec 32 is lossless");

  round_trip(FPZIP_TYPE_FLOAT, sizeof(float), field, copy, 0, 0);
  check(memcmp(field, copy, n * sizeof(float)) == 0, "float prec 0 is lossless");

  /* prec 16 keeps 7 mantissa bits: relative error below 2^-7. */
  round_trip(FPZIP_TYPE_FLOAT, sizeof(float), field, copy, 16, 0);
  for (i = 0; i < n; i++) {
    float d = copy[i] - field[i];
    float a = field[i] < 0 ? -field[i] : field[i];
    if (d < 0)
      d = -d;
    if (d > a / 128) {
      check(0, "float prec 16 error bound");
      break;
    }
  }
  free(copy);
  free(field);
}

static void test_double(void) {
  static const int precs[3] = {16, 32, 64};
  static const unsigned int sums[3] = {0x1582bfc9u, 0x4e9fa29du, 0x0dd20faau};
  double *field = double_field();
  double *copy = (double *)malloc(n * sizeof(double));
  int i;

  for (i = 0; i < 3; i++)
    round_trip(FPZIP_TYPE_DOUBLE, sizeof(double), field, copy, precs[i], sums[i]);
  check(memcmp(field, copy, n * sizeof(double)) == 0, "double prec 64 is lossless");

  round_trip(FPZIP_TYPE_DOUBLE, sizeof(double), field, copy, 0, 0);
  check(memcmp(field, copy, n * sizeof(double)) == 0, "double prec 0 is lossless");

  /* prec 32 keeps 20 mantissa bits: relative error below 2^-20. */
  round_trip(FPZIP_TYPE_DOUBLE, sizeof(double), field, copy, 32, 0);
  for (i = 0; i < n; i++) {
    double d = copy[i] - field[i];
    double a = field[i] < 0 ? -field[i] : field[i];
    if (d < 0)
      d = -d;
    if (d > a / 1048576.0) {
      check(0, "double prec 32 error bound");
      break;
    }
  }
  free(copy);
  free(field);
}

static void test_errors(void) {
  unsigned char zeros[64];
  FPZ *fpz;
  memset(zeros, 0, sizeof zeros);
  fpz = fpzip_read_from_buffer(zeros);
  check(fpzip_read_header(fpz) == 0, "header of a non-fpzip stream is refused");
  check(fpzip_errno == fpzipErrorBadFormat, "bad format error code");
  fpzip_read_close(fpz);
}

int main(void) {
  check(fpzip_library_version == 0x130u, "library version");
  check(strcmp(fpzip_version_string,
               "fpzip version 1.3.0 (December 20, 2019)") == 0,
        "version string");
  check(fpzip_codec_version == 0x11001u, "codec version");
  test_float();
  test_double();
  test_errors();
  if (failures)
    return 1;
  puts("ok");
  return 0;
}
