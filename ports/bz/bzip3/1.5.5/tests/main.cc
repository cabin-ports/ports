// Compresses compressible and incompressible inputs through the one-call API
// and the block API from C++, then checks that every byte comes back and that
// damaged input is rejected.
#include <libbz3.h>

#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

typedef std::vector<uint8_t> Bytes;

bool fail(const char *what) {
  std::fprintf(stderr, "bzip3 C++ test failed: %s\n", what);
  return false;
}

Bytes sample(std::size_t n, bool compressible) {
  Bytes v(n);
  uint32_t seed = 777;
  for (std::size_t i = 0; i < n; ++i) {
    seed = seed * 1664525u + 1013904223u;
    v[i] = compressible ? uint8_t(i * 7 % 251)
                        : uint8_t(seed >> 24);
  }
  return v;
}

bool round_trip(const char *what, std::size_t n, bool compressible) {
  const Bytes in = sample(n, compressible);
  Bytes packed(bz3_bound(n) + 64), back(n);
  std::size_t packed_size = packed.size(), back_size = back.size();
  if (bz3_compress(1 << 20, &in[0], &packed[0], n, &packed_size) != BZ3_OK)
    return fail(what);
  if (std::memcmp(&packed[0], "BZ3v1", 5) != 0)
    return fail("stream signature");
  if (compressible && packed_size >= n / 4) return fail("compressible size");
  if (bz3_decompress(&packed[0], &back[0], packed_size, &back_size) !=
      BZ3_OK)
    return fail(what);
  return back_size == n && back == in ? true : fail(what);
}

bool damage_is_rejected() {
  const Bytes in = sample(1000, true);
  Bytes packed(bz3_bound(1000) + 64), back(1000);
  std::size_t packed_size = packed.size(), back_size = back.size();
  if (bz3_compress(1 << 20, &in[0], &packed[0], in.size(), &packed_size) !=
      BZ3_OK)
    return fail("small compress");
  if (bz3_decompress(&packed[0], &back[0], packed_size - 1,
                     &back_size) != BZ3_ERR_TRUNCATED_DATA)
    return fail("truncated stream");
  // Bytes 13..20 are the block's sizes; the CRC-32 of the data follows.
  packed[21] ^= 0x55;
  back_size = back.size();
  if (bz3_decompress(&packed[0], &back[0], packed_size, &back_size) !=
      BZ3_ERR_CRC)
    return fail("damaged checksum");
  packed[21] ^= 0x55;
  packed[0] = 'X';
  back_size = back.size();
  if (bz3_decompress(&packed[0], &back[0], packed_size, &back_size) !=
      BZ3_ERR_MALFORMED_HEADER)
    return fail("bad signature");
  return true;
}

#ifndef _WIN32
bool parallel_blocks() {
  const int kBlock = 70000;
  bz3_state *states[2];
  Bytes bufs[2], orig[2];
  uint8_t *ptrs[2];
  int32_t sizes[2], orig_sizes[2];
  std::size_t buf_sizes[2];
  for (int i = 0; i < 2; ++i) {
    states[i] = bz3_new(kBlock);
    if (!states[i]) return fail("bz3_new");
    orig[i].resize(kBlock);
    for (int j = 0; j < kBlock; ++j)
      orig[i][j] = uint8_t((j * (i + 3)) % 241);
    bufs[i].assign(bz3_bound(kBlock), 0);
    std::memcpy(&bufs[i][0], &orig[i][0], kBlock);
    ptrs[i] = &bufs[i][0];
    sizes[i] = orig_sizes[i] = kBlock;
    buf_sizes[i] = bufs[i].size();
  }
  bz3_encode_blocks(states, ptrs, sizes, 2);
  bz3_decode_blocks(states, ptrs, buf_sizes, sizes, orig_sizes, 2);
  bool ok = true;
  for (int i = 0; i < 2; ++i) {
    if (bz3_last_error(states[i]) != BZ3_OK ||
        std::memcmp(ptrs[i], &orig[i][0], kBlock) != 0)
      ok = fail("parallel blocks");
    bz3_free(states[i]);
  }
  return ok;
}
#endif

}  // namespace

int main() {
  bool ok = true;
  if (std::strcmp(bz3_version(), "1.5.5") != 0) ok = fail("version");
  if (bz3_new(1000) != NULL) ok = fail("block size below the minimum");
  ok = round_trip("compressible", 150000, true) && ok;
  ok = round_trip("incompressible", 150000, false) && ok;
  ok = round_trip("tiny", 5, false) && ok;
  ok = damage_is_rejected() && ok;
#ifndef _WIN32
  ok = parallel_blocks() && ok;
#endif
  return ok ? 0 : 1;
}
