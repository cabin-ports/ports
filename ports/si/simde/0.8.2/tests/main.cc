// Runs simde's SSE4.2 CRC32C, its emulated 64-bit compare, AVX-512 masked
// arithmetic, WebAssembly SIMD and the half-precision conversions from C++,
// checked against known values and scalar results.
#include <simde/simde-f16.h>
#include <simde/wasm/simd128.h>
#include <simde/x86/avx512.h>
#include <simde/x86/sse4.2.h>

#include <float.h>
#include <stdint.h>
#include <stdio.h>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

}  // namespace

int main() {
  bool pass;

  // CRC32C's standard check value, byte by byte and four bytes at a time.
  const char digits[] = "123456789";
  uint32_t crc = ~UINT32_C(0);
  for (int i = 0; i < 9; ++i)
    crc = simde_mm_crc32_u8(crc, static_cast<uint8_t>(digits[i]));
  check(~crc == UINT32_C(0xE3069283), "_mm_crc32_u8");
  // "1234" as a little-endian word: the instruction takes its low byte first.
  uint32_t crc4 = simde_mm_crc32_u32(~UINT32_C(0), UINT32_C(0x34333231));
  for (int i = 4; i < 9; ++i)
    crc4 = simde_mm_crc32_u8(crc4, static_cast<uint8_t>(digits[i]));
  check(~crc4 == UINT32_C(0xE3069283), "_mm_crc32_u32");

  // Signed 64-bit greater-than, which SSE2 lacks: a pair whose difference
  // overflows, equal lanes, and lanes that differ only in the low half,
  // including its bit 31.
  const int64_t x[8] = {INT64_MIN, 5, 5, 3, 7, INT64_C(0xFFFFFFFF), -2, 1};
  const int64_t y[8] = {1, -5, 3, 5, 7, 1, -3, INT64_C(0x80000000)};
  int64_t gt[8];
  for (int i = 0; i < 8; i += 2)
    simde_mm_storeu_si128(gt + i, simde_mm_cmpgt_epi64(simde_mm_loadu_si128(x + i),
                                                       simde_mm_loadu_si128(y + i)));
  pass = true;
  for (int i = 0; i < 8; ++i)
    pass = pass && gt[i] == (x[i] > y[i] ? -1 : 0);
  check(pass, "_mm_cmpgt_epi64");

  // AVX-512: a masked add takes unselected lanes from src, and reductions
  // whose minimum (lane 2) and maximum (lane 11) sit in different halves.
  int32_t src[16], a[16], b[16], r[16];
  for (int i = 0; i < 16; ++i) {
    src[i] = 40000 + i;
    a[i] = (i * 7 + 2) % 16 * 1000 - 7000;
    b[i] = 3 - i * 17;
  }
  const simde__mmask16 mask = 0xA5C3;
  const simde__m512i va = simde_mm512_loadu_si512(a);
  simde_mm512_storeu_si512(r, simde_mm512_mask_add_epi32(simde_mm512_loadu_si512(src), mask, va,
                                                         simde_mm512_loadu_si512(b)));
  pass = true;
  for (int i = 0; i < 16; ++i)
    pass = pass && r[i] == (((mask >> i) & 1) ? a[i] + b[i] : src[i]);
  check(pass, "_mm512_mask_add_epi32");
  check(simde_mm512_reduce_min_epi32(va) == -7000 && simde_mm512_reduce_max_epi32(va) == 8000,
        "_mm512_reduce_min/max_epi32");

  // WebAssembly SIMD: saturating add.
  int16_t p[8] = {32000, -32000, 100, -100, 0, 1, -1, 12345};
  int16_t q[8] = {1000, -1000, 200, -200, 0, -1, 1, 20000};
  int16_t s[8];
  simde_wasm_v128_store(s, simde_wasm_i16x8_add_sat(simde_wasm_v128_load(p),
                                                    simde_wasm_v128_load(q)));
  pass = true;
  for (int i = 0; i < 8; ++i) {
    const int32_t v = int32_t(p[i]) + q[i];
    pass = pass && s[i] == (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
  }
  check(pass, "wasm_i16x8_add_sat");

  // Half precision, each direction against IEEE binary16 encodings: the
  // largest normal, the largest subnormal, round-to-nearest-even, and
  // overflow to infinity.
  const float values[] = {0.0f, 1.0f, -2.5f, 1.5f, 65504.0f, 1023.0f / 16777216.0f};
  const uint16_t encodings[] = {0x0000, 0x3C00, 0xC100, 0x3E00, 0x7BFF, 0x03FF};
  pass = true;
  for (int i = 0; i < 6; ++i) {
    const uint16_t bits = simde_float16_as_uint16(simde_float16_from_float32(values[i]));
    pass = pass && bits == encodings[i] &&
           simde_float16_to_float32(simde_uint16_as_float16(encodings[i])) == values[i];
  }
  const float inexact[] = {1.0f + 1.0f / 2048, 1.0f + 3.0f / 2048, 1.0f + 3.0f / 4096};
  const uint16_t rounded[] = {0x3C00, 0x3C02, 0x3C01};
  for (int i = 0; i < 3; ++i)
    pass = pass && simde_float16_as_uint16(simde_float16_from_float32(inexact[i])) == rounded[i];
  check(pass, "float16 conversions");
  check(simde_float16_as_uint16(simde_float16_from_float32(70000.0f)) == 0x7C00 &&
            simde_float16_to_float32(simde_uint16_as_float16(0x7C00)) > FLT_MAX,
        "float16 infinity");

  printf("simde (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
