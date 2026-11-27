#include <string>

#include <compoundfilereader.h>
#include <utf.h>

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

using Buf = std::vector<unsigned char>;

void put(Buf &b, size_t at, unsigned long long v, int bytes) {
  for (int i = 0; i < bytes; i++)
    b[at + i] = static_cast<unsigned char>(v >> (8 * i));
}

// Directory entry: name, type, child, left/right siblings, start sector, size.
void entry(Buf &b, int id, const char *name, int type, unsigned child,
           unsigned start, unsigned size) {
  size_t at = 512 * 2 + 128 * id;
  size_t n = 0;
  for (; name[n]; n++) put(b, at + 2 * n, name[n], 2);
  put(b, at + 64, 2 * (n + 1), 2);
  b[at + 66] = static_cast<unsigned char>(type);
  put(b, at + 68, 0xFFFFFFFF, 4);
  put(b, at + 72, 0xFFFFFFFF, 4);
  put(b, at + 76, child, 4);
  put(b, at + 116, start, 4);
  put(b, at + 120, size, 8);
}

// A version-3 compound file: header, FAT, directory, mini FAT, mini stream.
// Root Entry > storage "Dir" > stream "Data" holding "hello".
Buf build() {
  Buf b(512 * 5, 0);
  static const unsigned char sig[8] = {0xD0, 0xCF, 0x11, 0xE0,
                                       0xA1, 0xB1, 0x1A, 0xE1};
  for (int i = 0; i < 8; i++) b[i] = sig[i];
  put(b, 24, 0x3E, 2);
  put(b, 26, 3, 2);
  put(b, 28, 0xFFFE, 2);
  put(b, 30, 9, 2);
  put(b, 32, 6, 2);
  put(b, 44, 1, 4);
  put(b, 48, 1, 4);
  put(b, 56, 4096, 4);
  put(b, 60, 2, 4);
  put(b, 64, 1, 4);
  put(b, 68, 0xFFFFFFFE, 4);
  for (int i = 0; i < 109; i++) put(b, 76 + 4 * i, i ? 0xFFFFFFFF : 0, 4);

  size_t fat = 512;
  const unsigned fats[] = {0xFFFFFFFD, 0xFFFFFFFE, 0xFFFFFFFE, 0xFFFFFFFE};
  for (int i = 0; i < 128; i++) put(b, fat + 4 * i, i < 4 ? fats[i] : 0xFFFFFFFF, 4);

  entry(b, 0, "Root Entry", 5, 1, 3, 64);
  entry(b, 1, "Dir", 1, 2, 0, 0);
  entry(b, 2, "Data", 2, 0xFFFFFFFF, 0, 5);

  size_t minifat = 512 * 3;
  put(b, minifat, 0xFFFFFFFE, 4);
  for (int i = 1; i < 128; i++) put(b, minifat + 4 * i, 0xFFFFFFFF, 4);

  const char *hello = "hello";
  for (int i = 0; i < 5; i++) b[512 * 4 + i] = static_cast<unsigned char>(hello[i]);
  return b;
}

struct Seen {
  std::string name;
  std::string dir;
  int level;
  bool stream;
};

}  // namespace

int main() {
  Buf b = build();
  CFB::CompoundFileReader r(b.data(), b.size());

  check(r.GetFileInfo()->majorVersion == 3, "major version");
  const CFB::COMPOUND_FILE_ENTRY *root = r.GetRootEntry();
  check(UTF16ToUTF8(root->name) == "Root Entry", "root name");
  check(!r.IsStream(root), "root is a storage");

  std::vector<Seen> seen;
  const CFB::COMPOUND_FILE_ENTRY *data = nullptr;
  r.EnumFiles(root, -1,
              [&](const CFB::COMPOUND_FILE_ENTRY *e, const CFB::utf16string &dir,
                  int level) {
                std::string d;
                for (uint16_t c : dir) d += static_cast<char>(c);
                seen.push_back({UTF16ToUTF8(e->name), d, level, r.IsStream(e)});
                if (r.IsStream(e)) data = e;
              });
  check(seen.size() == 2, "two entries enumerated");
  if (seen.size() == 2) {
    check(seen[0].name == "Dir" && seen[0].level == 1 && !seen[0].stream &&
              seen[0].dir.empty(),
          "storage entry");
    // 0.1.0 leaves the name's terminating NUL in the path, so compare a prefix.
    check(seen[1].name == "Data" && seen[1].level == 2 && seen[1].stream &&
              seen[1].dir.compare(0, 3, "Dir") == 0,
          "stream entry");
  }

  if (data) {
    char buf[5] = {};
    r.ReadFile(data, 0, buf, 5);
    check(std::string(buf, 5) == "hello", "stream bytes");
    bool threw = false;
    try {
      r.ReadFile(data, 1, buf, 5);
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    check(threw, "read past the end throws");
  } else {
    check(false, "stream found");
  }

  Buf bad = b;
  bad[0] ^= 0xFF;
  bool wrong = false;
  try {
    CFB::CompoundFileReader x(bad.data(), bad.size());
  } catch (const CFB::WrongFormat &) {
    wrong = true;
  }
  check(wrong, "bad signature throws WrongFormat");

  bool corrupt = false;
  try {
    CFB::CompoundFileReader x(b.data(), 1024);
  } catch (const CFB::FileCorrupted &) {
    corrupt = true;
  }
  check(corrupt, "truncated file throws FileCorrupted");

  return failures ? 1 : 0;
}
