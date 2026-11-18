#include <crc_cpp.h>

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

constexpr std::array<std::uint8_t, 9> message{'1', '2', '3', '4', '5',
                                              '6', '7', '8', '9'};

template <typename Crc> constexpr auto checksum() {
  Crc crc;
  for (auto b : message) {
    crc.update(b);
  }
  return crc.final();
}

// The tiny, small and large tables use different generation and update code.
template <template <crc_cpp::table_size> class Family>
bool all_sizes(
    typename Family<crc_cpp::table_size::small>::accumulator_type check) {
  return checksum<Family<crc_cpp::table_size::tiny>>() == check &&
         checksum<Family<crc_cpp::table_size::small>>() == check &&
         checksum<Family<crc_cpp::table_size::large>>() == check;
}

// As upstream's test.cpp:73-86: computed at compile time.
static_assert(checksum<crc_cpp::crc8>() == 0xF4, "crc8 at compile time");

} // namespace

int main() {
  // Check values for "123456789" from the CRC RevEng catalogue.
  using namespace crc_cpp::family;
  CHECK(all_sizes<crc8>(0xF4));                  // CRC-8/SMBUS
  CHECK(all_sizes<crc8_maxim>(0xA1));            // CRC-8/MAXIM-DOW
  CHECK(all_sizes<crc16_ccit>(0x29B1));          // CRC-16/IBM-3740
  CHECK(all_sizes<crc16_augccit>(0xE5CC));       // CRC-16/SPI-FUJITSU
  CHECK(all_sizes<crc16_riello>(0x63D0));        // CRC-16/RIELLO
  CHECK(all_sizes<crc32>(0xCBF43926));           // CRC-32/ISO-HDLC
  CHECK(all_sizes<crc32_c>(0xE3069283));         // CRC-32/ISCSI
  CHECK(all_sizes<crc64_ecma>(0x6C40DF5F0B497347U)); // CRC-64/ECMA-182

  // The default (small) table, after reset().
  crc_cpp::crc32 crc;
  crc.update('x');
  crc.reset();
  for (auto b : message) {
    crc.update(b);
  }
  CHECK(crc.final() == 0xCBF43926);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("crc_cpp ok");
  return 0;
}
