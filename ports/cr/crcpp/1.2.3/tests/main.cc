// Consumer: catalogue CRCs from 3 to 64 bits against their published check
// values and an independent bit-serial model, through every calculation
// form: bit by bit and by table, whole and in parts, over bytes and over
// bit counts.  Valid on both of CRC.h's paths, C++11 and C++03.
#define CRCPP_INCLUDE_ESOTERIC_CRC_DEFINITIONS
#include <CRC.h>

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("main.cc:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

typedef unsigned long long u64;

unsigned char message[300];

u64 reflect(u64 value, int width) {
  u64 out = 0;
  for (int i = 0; i < width; ++i) out |= ((value >> i) & 1) << (width - 1 - i);
  return out;
}

// The Rocksoft model (Ross Williams, "A Painless Guide to CRC Error
// Detection Algorithms") shifting one message bit at a time; bits enter
// least significant first in each byte when the input is reflected.
template <typename T, crcpp_uint16 W>
u64 model(const CRC::Parameters<T, W>& p, const unsigned char* data, crcpp_size bits) {
  const u64 top = u64(1) << (W - 1);
  const u64 mask = top | (top - 1);
  u64 reg = u64(p.initialValue);
  for (crcpp_size i = 0; i < bits; ++i) {
    const unsigned byte = data[i / 8];
    const unsigned bit = (p.reflectInput ? byte >> (i % 8) : byte >> (7 - i % 8)) & 1;
    const bool feedback = ((reg & top) != 0) != (bit != 0);
    reg = (reg << 1) & mask;
    if (feedback) reg ^= u64(p.polynomial);
  }
  if (p.reflectOutput) reg = reflect(reg, W);
  return (reg ^ u64(p.finalXOR)) & mask;
}

template <typename T, crcpp_uint16 W>
void check(const char* name, const CRC::Parameters<T, W>& p, u64 check_value) {
  const int before = failures;
  // The catalogue's check value, the CRC of the ASCII digits 1 to 9, pins
  // both the parameters and the model.
  const unsigned char* digits = reinterpret_cast<const unsigned char*>("123456789");
  CHECK(model(p, digits, 72) == check_value);
  CHECK(u64(CRC::Calculate(digits, 9, p)) == check_value);
  const CRC::Table<T, W> table = p.MakeTable();
  CHECK(u64(CRC::Calculate(digits, 9, table)) == check_value);

  static const crcpp_size sizes[] = {0, 1, 7, 64, 300};
  for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
    const crcpp_size n = sizes[i], k = n / 3;
    const u64 whole = model(p, message, 8 * n);
    CHECK(u64(CRC::Calculate(message, n, p)) == whole);
    CHECK(u64(CRC::Calculate(message, n, table)) == whole);
    CHECK(u64(CRC::Calculate(message + k, n - k, p, CRC::Calculate(message, k, p))) == whole);
    CHECK(u64(CRC::Calculate(message + k, n - k, table, CRC::Calculate(message, k, table))) ==
          whole);
  }
  // CalculateBits takes whole bytes, the bits past the count zero
  // (README.md:92-93): the last byte's high bits when the input is
  // reflected, its low bits otherwise.
  static const crcpp_size bit_counts[] = {1, 3, 9, 13, 100, 2399};
  for (unsigned i = 0; i < sizeof bit_counts / sizeof bit_counts[0]; ++i) {
    const crcpp_size b = bit_counts[i], k = b / 16;
    unsigned char bits[sizeof message];
    std::memcpy(bits, message, (b + 7) / 8);
    if (b % 8 != 0) {
      const unsigned used = b % 8;
      bits[b / 8] &= p.reflectInput ? (1u << used) - 1 : 0xFFu << (8 - used);
    }
    const u64 whole = model(p, bits, b);
    CHECK(u64(CRC::CalculateBits(bits, b, p)) == whole);
    CHECK(u64(CRC::CalculateBits(bits, b, table)) == whole);
    CHECK(u64(CRC::CalculateBits(bits + k, b - 8 * k, p, CRC::Calculate(bits, k, p))) == whole);
    CHECK(u64(CRC::CalculateBits(bits + k, b - 8 * k, table, CRC::Calculate(bits, k, table))) ==
          whole);
  }
  if (failures != before) std::printf("  in %s\n", name);
}

}  // namespace

int main() {
  CHECK(CRCPP_MAJOR_VERSION == 1 && CRCPP_MINOR_VERSION == 2 && CRCPP_PATCH_VERSION == 3 &&
        CRCPP_REVISION_VERSION == 0);

  unsigned state = 12345;
  for (unsigned i = 0; i < sizeof message; ++i) {
    state = state * 1103515245u + 12345u;
    message[i] = static_cast<unsigned char>(state >> 16);
  }

  // Check values from Greg Cook's catalogue of parametrised CRC algorithms.
  check("CRC-4/G-704", CRC::CRC_4_ITU(), 0x7);
  check("CRC-5/USB", CRC::CRC_5_USB(), 0x19);
  check("CRC-6/CDMA2000-A", CRC::CRC_6_CDMA2000A(), 0x0D);
  check("CRC-6/G-704", CRC::CRC_6_ITU(), 0x06);
  check("CRC-7/MMC", CRC::CRC_7(), 0x75);
  check("CRC-8/SMBUS", CRC::CRC_8(), 0xF4);
  check("CRC-8/MAXIM-DOW", CRC::CRC_8_MAXIM(), 0xA1);
  check("CRC-8/WCDMA", CRC::CRC_8_WCDMA(), 0x25);
  check("CRC-10/ATM", CRC::CRC_10(), 0x199);
  check("CRC-11/FLEXRAY", CRC::CRC_11(), 0x5A3);
  check("CRC-12/UMTS", CRC::CRC_12_UMTS(), 0xDAF);
  check("CRC-13/BBC", CRC::CRC_13_BBC(), 0x04FA);
  check("CRC-15/CAN", CRC::CRC_15(), 0x059E);
  check("CRC-16/ARC", CRC::CRC_16_ARC(), 0xBB3D);
  check("CRC-16/IBM-3740", CRC::CRC_16_CCITTFALSE(), 0x29B1);
  check("CRC-16/GENIBUS", CRC::CRC_16_GENIBUS(), 0xD64E);
  check("CRC-16/KERMIT", CRC::CRC_16_KERMIT(), 0x2189);
  check("CRC-16/MODBUS", CRC::CRC_16_MODBUS(), 0x4B37);
  check("CRC-16/IBM-SDLC", CRC::CRC_16_X25(), 0x906E);
  check("CRC-16/XMODEM", CRC::CRC_16_XMODEM(), 0x31C3);
  check("CRC-17/CAN-FD", CRC::CRC_17_CAN(), 0x04F03);
  check("CRC-21/CAN-FD", CRC::CRC_21_CAN(), 0x0ED841);
  check("CRC-24/OPENPGP", CRC::CRC_24(), 0x21CF02);
  check("CRC-32/ISO-HDLC", CRC::CRC_32(), 0xCBF43926);
  check("CRC-32/BZIP2", CRC::CRC_32_BZIP2(), 0xFC891918);
  check("CRC-32/ISCSI", CRC::CRC_32_C(), 0xE3069283);
  check("CRC-32/MPEG-2", CRC::CRC_32_MPEG2(), 0x0376E6E7);
  check("CRC-32/CKSUM", CRC::CRC_32_POSIX(), 0x765E7680);
  check("CRC-40/GSM", CRC::CRC_40_GSM(), 0xD4164FC646ULL);
  check("CRC-64/ECMA-182", CRC::CRC_64(), 0x6C40DF5F0B497347ULL);

  // Parameters a consumer defines.
  const CRC::Parameters<crcpp_uint8, 3> gsm3 = {0x3, 0x0, 0x7, false, false};
  check("CRC-3/GSM", gsm3, 0x4);
  const CRC::Parameters<crcpp_uint64, 64> xz = {0x42F0E1EBA9EA3693ULL, ~0ULL, ~0ULL, true, true};
  check("CRC-64/XZ", xz, 0x995DC9BBDF1939FAULL);

  // CalculateBits without a previous CRC leaves the initial value
  // unreflected, which a reflected CRC with a non-palindromic one exposes;
  // continuing from Calculate over no bytes gives the right CRC.
  const CRC::Parameters<crcpp_uint16, 16> riello = {0x1021, 0xB2AA, 0x0000, true, true};
  const unsigned char* digits = reinterpret_cast<const unsigned char*>("123456789");
  CHECK(CRC::Calculate(digits, 9, riello) == 0x63D0);
  CHECK(CRC::CalculateBits(digits, 72, riello, CRC::Calculate(digits, 0, riello)) == 0x63D0);

  // The well-known reflected CRC-32 table.
  const CRC::Table<crcpp_uint32, 32> table(CRC::CRC_32());
  CHECK(table.GetParameters().polynomial == 0x04C11DB7);
  CHECK(table[1] == 0x77073096 && table[128] == 0xEDB88320 && table.GetTable()[255] == 0x2D02EF8D);

  std::printf("crcpp: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
