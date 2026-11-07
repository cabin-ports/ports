// Consumer test for cabin-ports/streamvbyte from C++: a signed series
// stored as zigzag deltas in a std::vector stream, and a sparse array for
// which the 0124 format beats the standard one.
#include <streamvbyte.h>
#include <streamvbyte_zigzag.h>
#include <streamvbytedelta.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  // A slowly drifting signed series: small zigzag deltas, one byte each.
  const uint32_t n = 513;
  std::vector<int32_t> series(n);
  for (uint32_t i = 0; i < n; ++i)
    series[i] = -1000 + static_cast<int32_t>((i * 7) % 61) - static_cast<int32_t>(i % 5);

  std::vector<uint32_t> zz(n);
  zigzag_delta_encode(series.data(), zz.data(), n, -1000);
  std::vector<uint8_t> stream(streamvbyte_max_compressedbytes(n));
  const size_t written = streamvbyte_encode(zz.data(), n, stream.data());
  check(written == (n + 3) / 4 + n, "small zigzag deltas take one byte each");
  check(streamvbyte_validate_stream(stream.data(), written, n), "the stream validates");

  std::vector<uint32_t> zz_back(n);
  check(streamvbyte_decode(stream.data(), zz_back.data(), n) == written,
        "decode reads the whole stream");
  std::vector<int32_t> back(n);
  zigzag_delta_decode(zz_back.data(), back.data(), n, -1000);
  check(back == series, "zigzag delta round trip");

  // Mostly zeros: 0124 stores them in the keys alone.
  std::vector<uint32_t> sparse(n, 0);
  for (uint32_t i = 0; i < n; i += 16)
    sparse[i] = 0xABCD0000u + i;
  const size_t standard = streamvbyte_compressedbytes(sparse.data(), n);
  const size_t compact = streamvbyte_encode_0124(sparse.data(), n, stream.data());
  check(compact == (n + 3) / 4 + 4 * ((n + 15) / 16), "0124 spends no data on zeros");
  check(compact < standard, "0124 is smaller for sparse data");
  std::vector<uint32_t> sparse_back(n, 1);
  check(streamvbyte_decode_0124(stream.data(), sparse_back.data(), n) == compact,
        "0124 decode reads the whole stream");
  check(sparse_back == sparse, "0124 round trip");
  check(!streamvbyte_validate_stream_0124(stream.data(), compact - 4, n),
        "a truncated 0124 stream fails");

  std::printf("streamvbyte (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
