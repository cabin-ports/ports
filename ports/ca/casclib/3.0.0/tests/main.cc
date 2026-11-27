#include <CascLib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// The local BLTE file of main.c: frame 1 stored ('N' + "hello "), frame 2
// zlib-compressed ('Z' + "casclib"), each with the MD5 of its encoded bytes.
const unsigned char blte[] = {
    'B',  'L',  'T',  'E',  0x00, 0x00, 0x00, 0x3c, 0x0f, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x51, 0x09, 0x6e, 0x80,
    0x34, 0x14, 0x4a, 0x1b, 0x7f, 0xeb, 0x43, 0x85, 0x62, 0x7d, 0xfd, 0x40,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x56, 0xe8, 0x63,
    0xcb, 0x06, 0x92, 0xda, 0x25, 0x53, 0xb9, 0xc5, 0x3c, 0xe6, 0x0e, 0x3b,
    'N',  'h',  'e',  'l',  'l',  'o',  ' ',  'Z',  0x78, 0xda, 0x4b, 0x4e,
    0x2c, 0x4e, 0xce, 0xc9, 0x4c, 0x02, 0x00, 0x0b, 0x45, 0x02, 0xd2,
};
// Offset of the first byte of frame 2's MD5.
const std::size_t frame2_hash = 44;

bool write_file(const std::string &path, const unsigned char *data,
                std::size_t size) {
  std::ofstream out(path.c_str(), std::ios::binary);
  out.write(reinterpret_cast<const char *>(data),
            static_cast<std::streamsize>(size));
  out.close();
  return !out.fail();
}

}  // namespace

int main() {
  const char *dir = std::getenv("CABIN_PROFILE_DIR");
  if (dir == NULL) {
    std::fprintf(stderr, "FAIL: CABIN_PROFILE_DIR\n");
    return 1;
  }
  const std::string good = std::string(dir) + "/casclib-probe-cc-good.blte";
  const std::string bad = std::string(dir) + "/casclib-probe-cc-bad.blte";
  const std::string missing = std::string(dir) + "/casclib-probe-cc-no-storage";

  check(CASCLIB_VERSION == 0x0300, "CASCLIB_VERSION");
  check(std::string(CASCLIB_VERSION_STRING) == "3.0", "CASCLIB_VERSION_STRING");
  check(std::string(CascCdnGetDefault()) == "http://us.patch.battle.net:1119",
        "CascCdnGetDefault");

  HANDLE h = NULL;
  check(!CascOpenLocalFile("", 0, &h), "CascOpenLocalFile(\"\") fails");
  check(GetCascError() == ERROR_INVALID_PARAMETER,
        "CascOpenLocalFile(\"\") sets ERROR_INVALID_PARAMETER");

  check(!CascOpenStorage(missing.c_str(), 0, &h),
        "CascOpenStorage(missing) fails");
  check(GetCascError() == ERROR_FILE_NOT_FOUND,
        "CascOpenStorage(missing) sets ERROR_FILE_NOT_FOUND");

  // Two reads: the first stops inside frame 2, which the second finishes.
  check(write_file(good, blte, sizeof blte), "write good BLTE file");
  h = NULL;
  check(CascOpenLocalFile(good.c_str(), CASC_STRICT_DATA_CHECK, &h) &&
            h != NULL,
        "CascOpenLocalFile(good)");
  if (h != NULL) {
    char buf[64] = {0};
    DWORD first = 0, second = 0, third = 1;
    check(CascReadFile(h, buf, 8, &first) && first == 8, "first read: 8 bytes");
    check(CascReadFile(h, buf + first, sizeof buf - first, &second) &&
              second == 5,
          "second read: the remaining 5 bytes");
    check(std::string(buf, first + second) == "hello casclib",
          "decoded content is \"hello casclib\"");
    check(CascReadFile(h, buf, sizeof buf, &third) && third == 0,
          "read at end of file returns 0 bytes");
    check(CascCloseFile(h), "CascCloseFile(good)");
  }

  // With a wrong hash on the zlib frame, a CASC_STRICT_DATA_CHECK read still
  // returns frame 1 and then fails on frame 2.
  unsigned char corrupt[sizeof blte];
  std::memcpy(corrupt, blte, sizeof blte);
  corrupt[frame2_hash] ^= 0xff;
  check(write_file(bad, corrupt, sizeof corrupt), "write bad BLTE file");
  h = NULL;
  check(CascOpenLocalFile(bad.c_str(), CASC_STRICT_DATA_CHECK, &h) && h != NULL,
        "CascOpenLocalFile(bad)");
  if (h != NULL) {
    char buf[64];
    DWORD first = 0, got = 1;
    check(CascReadFile(h, buf, 6, &first) && first == 6 &&
              std::memcmp(buf, "hello ", 6) == 0,
          "CascReadFile(bad) returns frame 1");
    check(!CascReadFile(h, buf, sizeof buf, &got) && got == 0,
          "CascReadFile(bad) fails on frame 2");
    check(GetCascError() == ERROR_FILE_CORRUPT,
          "CascReadFile(bad) sets ERROR_FILE_CORRUPT");
    CascCloseFile(h);
  }

  std::remove(good.c_str());
  std::remove(bad.c_str());
  if (failures != 0) return 1;
  std::printf("casclib C++ consumer OK\n");
  return 0;
}
