/* The C consumer: upstream's own small test fields (tests/testzfp.cpp:
 * 43-267, 293-308) compressed in each mode and held to the data checksums,
 * compressed sizes and maximum errors upstream's test expects
 * (testzfp.cpp:792-961); bit-exact reversible round trips of both integer
 * types in one to four dimensions; a full-header round trip and a
 * corrupt header; the low-level block calls; and the serial build. */
#include <zfp.h>

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int pass, const char* what)
{
  if (!pass) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

/* 2^12 scalars in each dimensionality: 4096, 64^2, 16^3 and 8^4. */
#define N 4096
static const size_t edge[4] = { 4096, 64, 16, 8 };

/* testzfp.cpp's refine1d-refine4d: interpolate f[m^dims] to g[(2m)^dims].
 * Upstream sums in int, which wraps in 4D (|s| reaches 2^32); unsigned
 * arithmetic reproduces that without overflow. */
static void refine(int* g, const int* f, size_t m, unsigned dims)
{
  static const int weight[4] = { -1, 9, 9, -1 };
  size_t n = 2 * m, count = 1, t, q;
  unsigned d;
  for (d = 0; d < dims; d++)
    count *= n;
  for (t = 0; t < count; t++) {
    size_t c[4], rest = t;
    unsigned s = 0;
    for (d = 0; d < dims; d++) {
      c[d] = rest % n;
      rest /= n;
    }
    for (q = 0; q < ((size_t)1 << (2 * dims)); q++) {
      int w = 1;
      size_t index = 0, stride = 1;
      for (d = 0; d < dims; d++) {
        size_t i = (q >> (2 * d)) & 3u;
        size_t cc = c[d] & 1u ? (c[d] / 2 + i - 1 + m) % m : c[d] / 2;
        w *= weight[i];
        index += cc * stride;
        stride *= m;
      }
      s += (unsigned)(w * f[index]);
    }
    g[t] = (int)s / (1 << (4 * dims));
  }
}

/* testzfp.cpp's gen_array_1d-gen_array_4d as integers. */
static void generate(int* out, unsigned dims)
{
  int* f = malloc(N * sizeof(int));
  int* g = malloc(N * sizeof(int));
  size_t m, i, count = (size_t)1 << (2 * dims);
  for (i = 0; i < count; i++) {
    size_t rest = i, parity = 0, inner = 1;
    unsigned d;
    for (d = 0; d < dims; d++) {
      size_t x = rest % 4;
      rest /= 4;
      parity ^= x;
      inner &= x == 1 || x == 2;
    }
    f[i] = inner ? 0x10000 * (1 - 2 * (int)(parity & 1u)) : 0;
  }
  for (m = 4; m < edge[dims - 1]; m *= 2) {
    int* t = f;
    refine(g, f, m, dims);
    f = g;
    g = t;
  }
  memcpy(out, f, N * sizeof(int));
  free(g);
  free(f);
}

/* testzfp.cpp's hash: Jenkins one-at-a-time over the bytes. */
static uint32 hash(const void* p, size_t n)
{
  const unsigned char* q = p;
  uint32 h = 0;
  for (; n; q++, n--) {
    h += *q;
    h += h << 10;
    h ^= h >> 6;
  }
  h += h << 3;
  h ^= h >> 11;
  h += h << 15;
  return h;
}

static zfp_field* make_field(zfp_type type, void* data, unsigned dims)
{
  size_t e = edge[dims - 1];
  switch (dims) {
    case 1: return zfp_field_1d(data, type, e);
    case 2: return zfp_field_2d(data, type, e, e);
    case 3: return zfp_field_3d(data, type, e, e, e);
    default: return zfp_field_4d(data, type, e, e, e, e);
  }
}

/* The maximum error in the scalar type, as upstream computes it. */
static double max_error(zfp_type type, const void* a, const void* b)
{
  size_t i;
  if (type == zfp_type_float) {
    const float* x = a;
    const float* y = b;
    float e = 0;
    for (i = 0; i < N; i++)
      if (fabsf(x[i] - y[i]) > e)
        e = fabsf(x[i] - y[i]);
    return e;
  } else {
    const double* x = a;
    const double* y = b;
    double e = 0;
    for (i = 0; i < N; i++)
      if (fabs(x[i] - y[i]) > e)
        e = fabs(x[i] - y[i]);
    return e;
  }
}

/* Compress `in` with the stream's current mode into a fresh buffer and
 * decompress it into `out`; returns the compressed size, or 0. */
static size_t round_trip(zfp_stream* zfp, zfp_field* in, void* out, unsigned char** buffer)
{
  size_t size = zfp_stream_maximum_size(zfp, in), bytes;
  zfp_field decoded = *in;
  bitstream* stream;
  *buffer = malloc(size);
  stream = stream_open(*buffer, size);
  zfp_stream_set_bit_stream(zfp, stream);
  zfp_stream_rewind(zfp);
  bytes = zfp_compress(zfp, in);
  zfp_field_set_pointer(&decoded, out);
  zfp_stream_rewind(zfp);
  if (zfp_decompress(zfp, &decoded) != bytes)
    bytes = 0;
  stream_close(stream);
  return bytes;
}

/* testzfp.cpp:792-961, the small arrays: [type][dims][...]. */
static const uint32 checksums[2][4] = {
  { 0x54174c44u, 0x86609589u, 0xfc0a6a76u, 0xa3481e00u },
  { 0x7d257bb6u, 0x294bb210u, 0x68614d26u, 0xf6bd3a21u },
};
static const double rate_emax[2][4][4] = {
  { { 1.627e+01, 8.277e-02, 0.000e+00 },
    { 1.500e+00, 3.663e-03, 0.000e+00 },
    { 1.500e+00, 9.583e-03, 0.000e+00 },
    { 1.373e+01, 6.633e-01, 0.000e+00 } },
  { { 1.627e+01, 1.601e+01, 1.832e-04, 0.000e+00 },
    { 2.376e+01, 1.797e-01, 8.584e-06, 0.000e+00 },
    { 5.210e+00, 2.002e-01, 3.338e-05, 0.000e+00 },
    { 1.016e+01, 8.985e+00, 3.312e-03, 0.000e+00 } },
};
static const size_t precision_bytes[2][4][3] = {
  { { 2192, 3280, 6328 }, { 592, 1328, 4384 }, { 152, 1040, 4600 }, { 64, 1760, 5856 } },
  { { 3664, 6712, 14104 }, { 1424, 4480, 12616 }, { 1064, 4624, 12808 }, { 1768, 5864, 14056 } },
};
static const size_t accuracy_bytes[2][4][3] = {
  { { 6328, 11944, 13720 }, { 4936, 11064, 12520 }, { 6104, 11752, 12784 }, { 9440, 14048, 14048 } },
  { { 6712, 25888, 29064 }, { 5032, 26016, 28984 }, { 6128, 27120, 29192 }, { 9448, 30440, 30440 } },
};
static const size_t reversible_bytes[2][4] = {
  { 7272, 5104, 6096, 6864 },
  { 7784, 5232, 6128, 6872 },
};

/* testzfp.cpp's test() for one scalar type and dimensionality. */
static void test_real(zfp_type type, unsigned dims)
{
  const unsigned t = type == zfp_type_double;
  const size_t scalar = t ? sizeof(double) : sizeof(float);
  const unsigned one = 1;
  int* ints = malloc(N * sizeof(int));
  void* f = malloc(N * sizeof(double));
  void* g = malloc(N * sizeof(double));
  zfp_field* field = make_field(type, f, dims);
  zfp_stream* zfp = zfp_stream_open(NULL);
  unsigned char* buffer;
  unsigned rate, i, precision;
  char what[96];
  size_t k, bytes;

  generate(ints, dims);
  for (k = 0; k < N; k++)
    if (t)
      ((double*)f)[k] = ldexp((double)ints[k], -12);
    else
      ((float*)f)[k] = ldexpf((float)ints[k], -12);
  /* upstream's checksums hash little-endian scalars */
  if (*(const unsigned char*)&one) {
    snprintf(what, sizeof what, "%uD %s data checksum", dims, t ? "double" : "float");
    check(hash(f, N * scalar) == checksums[t][dims - 1], what);
  }

  for (rate = 2u >> t, i = 0; rate <= 32 * (t + 1); rate *= 4, i++) {
    double emax = rate_emax[t][dims - 1][i];
    snprintf(what, sizeof what, "%uD %s rate %u", dims, t ? "double" : "float", rate);
    /* the rate actually set: at least a sign and exponent per block */
    double actual = zfp_stream_set_rate(zfp, rate, type, dims, zfp_false);
    check(actual >= rate, what);
    bytes = round_trip(zfp, field, g, &buffer);
    check(bytes == (size_t)floor(actual * N / 8 + 0.5), what);
    check(max_error(type, f, g) <= (t ? emax : (float)emax), what);
    free(buffer);
  }

  for (precision = 4u << t, i = 0; i < 3; precision *= 2, i++) {
    snprintf(what, sizeof what, "%uD %s precision %u", dims, t ? "double" : "float", precision);
    zfp_stream_set_precision(zfp, precision);
    check(zfp_stream_compression_mode(zfp) == zfp_mode_fixed_precision, what);
    check(round_trip(zfp, field, g, &buffer) == precision_bytes[t][dims - 1][i], what);
    free(buffer);
  }

  for (i = 0; i < 3; i++) {
    double tol = i == 0 ? 1e-3 : i == 1 ? 2 * (t ? DBL_EPSILON : FLT_EPSILON) : 0;
    double actual = zfp_stream_set_accuracy(zfp, t ? tol : (float)tol);
    snprintf(what, sizeof what, "%uD %s accuracy %g", dims, t ? "double" : "float", tol);
    check(round_trip(zfp, field, g, &buffer) == accuracy_bytes[t][dims - 1][i], what);
    if (tol > 0)
      check(max_error(type, f, g) <= (t ? actual : (float)actual), what);
    free(buffer);
  }

  snprintf(what, sizeof what, "%uD %s reversible", dims, t ? "double" : "float");
  zfp_stream_set_reversible(zfp);
  check(zfp_stream_compression_mode(zfp) == zfp_mode_reversible, what);
  check(round_trip(zfp, field, g, &buffer) == reversible_bytes[t][dims - 1], what);
  check(memcmp(f, g, N * scalar) == 0, what);
  free(buffer);

  zfp_stream_close(zfp);
  zfp_field_free(field);
  free(g);
  free(f);
  free(ints);
}

/* Bit-exact reversible round trips of int32 and int64 fields. */
static void test_integers(unsigned dims)
{
  int* ints = malloc(N * sizeof(int));
  int64* wide = malloc(N * sizeof(int64));
  int64* back = malloc(N * sizeof(int64));
  zfp_stream* zfp = zfp_stream_open(NULL);
  zfp_field* field;
  unsigned char* buffer;
  char what[64];
  size_t k;

  generate(ints, dims);
  zfp_stream_set_reversible(zfp);

  field = make_field(zfp_type_int32, ints, dims);
  snprintf(what, sizeof what, "%uD int32 reversible", dims);
  check(round_trip(zfp, field, back, &buffer) != 0, what);
  check(memcmp(ints, back, N * sizeof(int)) == 0, what);
  free(buffer);
  zfp_field_free(field);

  for (k = 0; k < N; k++)
    wide[k] = (int64)ints[k] * 0x100000 + (int64)(k % 7) - 3;
  field = make_field(zfp_type_int64, wide, dims);
  snprintf(what, sizeof what, "%uD int64 reversible", dims);
  check(round_trip(zfp, field, back, &buffer) != 0, what);
  check(memcmp(wide, back, N * sizeof(int64)) == 0, what);
  free(buffer);
  zfp_field_free(field);

  zfp_stream_close(zfp);
  free(back);
  free(wide);
  free(ints);
}

/* A full header carries the field and the mode to a fresh stream. */
static void test_header(void)
{
  double* f = malloc(N * sizeof(double));
  double* g = malloc(N * sizeof(double));
  int* ints = malloc(N * sizeof(int));
  zfp_field* field = zfp_field_3d(f, zfp_type_double, 16, 16, 16);
  zfp_field* read = zfp_field_alloc();
  zfp_stream* zfp = zfp_stream_open(NULL);
  zfp_stream* other = zfp_stream_open(NULL);
  double tolerance = zfp_stream_set_accuracy(zfp, 1e-6);
  size_t size, header, bytes, k;
  unsigned char* buffer;
  bitstream* stream;

  generate(ints, 3);
  for (k = 0; k < N; k++)
    f[k] = ldexp((double)ints[k], -12);
  size = zfp_stream_maximum_size(zfp, field);
  buffer = malloc(size);
  stream = stream_open(buffer, size);
  zfp_stream_set_bit_stream(zfp, stream);
  header = zfp_write_header(zfp, field, ZFP_HEADER_FULL);
  bytes = zfp_compress(zfp, field);
  check(header != 0 && bytes > 0 && bytes < N * sizeof(double), "header compress");

  zfp_stream_set_bit_stream(other, stream);
  zfp_stream_rewind(other);
  check(zfp_read_header(other, read, ZFP_HEADER_FULL) == header, "header read");
  check(zfp_field_type(read) == zfp_type_double && zfp_field_dimensionality(read) == 3, "header field type");
  check(read->nx == 16 && read->ny == 16 && read->nz == 16, "header field size");
  check(zfp_stream_compression_mode(other) == zfp_mode_fixed_accuracy, "header mode");
  check(zfp_stream_accuracy(other) == tolerance, "header tolerance");
  zfp_field_set_pointer(read, g);
  check(zfp_decompress(other, read) == bytes, "header decompress");
  check(max_error(zfp_type_double, f, g) <= tolerance, "header error");

  /* the magic leads the first stream word in either byte order */
  for (k = 0; k < 8; k++)
    buffer[k] ^= 0xff;
  zfp_stream_rewind(other);
  check(zfp_read_header(other, read, ZFP_HEADER_FULL) == 0, "corrupt magic refused");

  zfp_stream_close(other);
  zfp_stream_close(zfp);
  stream_close(stream);
  zfp_field_free(read);
  zfp_field_free(field);
  free(buffer);
  free(ints);
  free(g);
  free(f);
}

/* Fixed-rate blocks take exactly rate * 4^dims bits. */
static void test_blocks(void)
{
  double block[16], decoded[16];
  uint64 buffer[128];
  zfp_stream* zfp = zfp_stream_open(NULL);
  bitstream* stream = stream_open(buffer, sizeof buffer);
  size_t i, bits;
  double e = 0;

  for (i = 0; i < 16; i++)
    block[i] = sin((double)i) * 100;
  zfp_stream_set_bit_stream(zfp, stream);
  zfp_stream_set_rate(zfp, 32, zfp_type_double, 2, zfp_false);
  bits = zfp_encode_block_double_2(zfp, block);
  check(bits == 32 * 16, "block encode bits");
  zfp_stream_flush(zfp);
  zfp_stream_rewind(zfp);
  check(zfp_decode_block_double_2(zfp, decoded) == bits, "block decode bits");
  for (i = 0; i < 16; i++)
    if (fabs(block[i] - decoded[i]) > e)
      e = fabs(block[i] - decoded[i]);
  check(e > 0 && e < 1e-4, "block error");
  stream_close(stream);
  zfp_stream_close(zfp);
}

int main(void)
{
  zfp_stream* zfp = zfp_stream_open(NULL);
  unsigned dims;

  check(zfp_library_version == ZFP_VERSION && ZFP_VERSION_MAJOR == 1 && ZFP_VERSION_MINOR == 0 &&
        ZFP_VERSION_PATCH == 1, "library version");
  check(zfp_codec_version == ZFP_CODEC && ZFP_CODEC == 5, "codec version");
  check(strstr(zfp_version_string, "1.0.1") != NULL, "version string");
  check(stream_word_bits == 64, "64-bit stream words");

  /* The serial build: OpenMP execution is refused. */
  check(zfp_stream_set_execution(zfp, zfp_exec_omp) == zfp_false, "OpenMP refused");
  check(zfp_stream_execution(zfp) == zfp_exec_serial, "serial execution");
  check(zfp_stream_set_execution(zfp, zfp_exec_serial) == zfp_true, "serial accepted");
  zfp_stream_close(zfp);

  for (dims = 1; dims <= 4; dims++) {
    test_real(zfp_type_float, dims);
    test_real(zfp_type_double, dims);
    test_integers(dims);
  }
  test_header();
  test_blocks();

  if (failures)
    return EXIT_FAILURE;
  puts("zfp: ok");
  return EXIT_SUCCESS;
}
