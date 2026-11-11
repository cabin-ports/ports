#include <neko/function/detectFileType.hpp>
#include <neko/function/pattern.hpp>
#include <neko/function/utilities.hpp>
#include <neko/function/uuid.hpp>
#include <neko/schema/exception.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

template <class E, class F>
bool throws(F f) {
  try {
    f();
  } catch (const E&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

// 2024-06-07T15:04:05Z.
constexpr std::time_t kStamp = 1717772645;

// The ||, >>= cases use the shapes cabin.toml's limitations allow.
void operators() {
  std::vector<int> v{1, 2, 3, 4};
  {
    using namespace neko::ops::pipe;
    CHECK((5 | [](int x) { return x * 2; }) == 10);
  }
  {
    using namespace neko::ops::ranges;
    int total = v | [](const std::vector<int>& r) {
      return std::accumulate(r.begin(), r.end(), 0);
    };
    CHECK(total == 10);
  }
  {
    using namespace neko::ops::foreach;
    int total = 0;
    v | [&total](int x) { total += x * x; };
    CHECK(total == 30);
  }
  {
    using namespace neko::ops::logic;
    auto half = [](int x) { return x % 2 == 0 ? std::optional<int>(x / 2) : std::nullopt; };
    CHECK((std::optional<int>(84) >>= half) == std::optional<int>(42));
    CHECK(!(std::optional<int>(7) >>= half));
    CHECK(!(std::optional<int>() >>= half));
  }
  {
    using namespace neko::ops::fallback;
    static int stored = 7;
    static int fallbackValue = -1;
    auto lookup = [](int k) -> int* { return k > 0 ? &stored : nullptr; };
    auto otherwise = [](int) -> int& { return fallbackValue; };
    auto get = lookup || otherwise;
    CHECK(get(1) == 7);
    CHECK(get(0) == -1);
  }
}

void strings() {
  namespace s = neko::util::string;
  CHECK(s::wrapWith("hi") == "\"hi\"");
  CHECK(s::wrapWith(std::string("x"), "<", ">") == "<x>");
  CHECK(s::toLower(std::string("MiXeD 42")) == "mixed 42");
  CHECK(s::toUpper(std::string("MiXeD 42")) == "MIXED 42");
  CHECK(s::getExtensionName("Archive.TAR.GZ") == "gz");
  CHECK(s::getExtensionName("Archive.TAR.GZ", true) == "GZ");
  CHECK(s::getExtensionName("Makefile").empty());
  CHECK(s::matchExtensionName("/p/File.TXT", "txt"));
  CHECK(!s::matchExtensionName("/p/File.TXT", "txt", true));
  CHECK(s::matchExtensionNames("notes.md", {"txt", "md"}));
  CHECK(!s::matchExtensionNames("notes.mdx", {"txt", "md"}));
  CHECK(s::convertToUnixPath(std::string("C:\\a\\b.txt")) == "C:/a/b.txt");
}

void memoryAndLambdas() {
  namespace u = neko::util;
  auto a = u::memory::makeArray(3, 1, 2);
  static_assert(std::is_same_v<decltype(a), std::array<int, 3>>);
  CHECK(a[0] == 3 && a[2] == 2);

  // The deep-copy overload takes a pointer to const; a plain int* binds to
  // the value overload and is returned as is.
  int x = 5;
  const int* cx = &x;
  std::unique_ptr<int> p(u::memory::copy(cx));
  CHECK(p && p.get() != &x && *p == 5);
  CHECK(u::memory::copy(&x) == &x);
  const int* none = nullptr;
  CHECK(u::memory::copy(none) == nullptr);
  int* q = u::deepCopy(&x);
  CHECK(q != &x && *q == 5);
  delete q;

  CHECK(u::plusDoubleQuote(std::string("a")) == "\"a\"");
  CHECK(u::plusSingleQuote(std::string("a")) == "'a'");
  CHECK(u::unifiedPath(std::string("a\\b\\c")) == "a/b/c");
  std::shared_ptr<int> sp = u::makeShared(9);
  CHECK(sp && *sp == 9);
  CHECK(u::copy(std::string("dup")) == "dup");
}

void logicAndMath() {
  namespace u = neko::util;
  static_assert(u::sum(1, 2, 3) == 6);
  static_assert(u::product(2, 3, 4) == 24);
  static_assert(u::allTrue(true, true) && !u::allTrue(true, false));
  static_assert(u::anyTrue(false, true) && !u::anyTrue(false, false));
  CHECK(u::sum(1, 2, 3.5) == 6.5);
  CHECK(u::boolTo(true) == "true");
  CHECK(u::boolTo(false) == "false");
  CHECK(u::boolTo<int>(false, 1, 0) == 0);
}

void setTimeZone() {
  // Nine hours east of UTC, without daylight saving time.
#if defined(_WIN32)
  _putenv_s("TZ", "JST-9");
  _tzset();
#else
  setenv("TZ", "JST-9", 1);
  tzset();
#endif
}

void times() {
  namespace t = neko::util::time;
  std::tm tm{};
  tm.tm_year = 2024 - 1900;
  tm.tm_mon = 5;
  tm.tm_mday = 7;
  tm.tm_hour = 15;
  tm.tm_min = 4;
  tm.tm_sec = 5;
  CHECK(t::toUtcTimeT(tm) == kStamp);

  std::tm utc = t::toUtcTm(kStamp);
  CHECK(utc.tm_year == 124 && utc.tm_mon == 5 && utc.tm_mday == 7);
  CHECK(utc.tm_hour == 15 && utc.tm_min == 4 && utc.tm_sec == 5);
  std::tm local = t::toLocalTm(kStamp);
  CHECK(local.tm_mday == 8 && local.tm_hour == 0 && local.tm_min == 4);

  CHECK(t::getUtcZTimeString(kStamp) == "2024-06-07T15:04:05Z");
  CHECK(t::getLocalTimeString("%Y-%m-%d %H:%M:%S", kStamp) == "2024-06-08 00:04:05");
  CHECK(t::timeToString(42) == "42");
  // A second of slack: std::time may read a coarser clock than system_clock.
  std::time_t before = std::time(nullptr);
  std::time_t now = t::getUtcNow();
  std::time_t after = std::time(nullptr);
  CHECK(now >= before - 1 && now <= after + 1);

  CHECK(t::parseToUTCTime("2024-06-07T15:04:05Z") == kStamp);
  CHECK(t::parseToUTCTime("2024-06-07T15:04:05") == kStamp);
  CHECK(t::parseToUTCTime("2024-06-08T00:04:05+09:00") == kStamp);
  CHECK(t::parseToUTCTime("2024-06-07t10:04:05.250-0500") == kStamp);
  CHECK(!t::parseToUTCTime("2024-06-07 15:04:05"));
  CHECK(!t::parseToUTCTime("not a date"));
  CHECK(t::parseToUTCTimeString("2024-06-08T00:04:05+09:00") == "2024-06-07T15:04:05Z");
  CHECK(t::parseToLocalTimeString("2024-06-07T15:04:05Z") == "2024-06-08 00:04:05");
  CHECK(!t::parseToLocalTimeString("2024/06/07"));
}

void base64() {
  using neko::util::base64::base64Decode;
  using neko::util::base64::base64Encode;
  // RFC 4648, section 10.
  const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char* coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  for (int i = 0; i < 7; ++i) {
    CHECK(base64Encode(plain[i]) == coded[i]);
    CHECK(base64Decode(coded[i]) == plain[i]);
  }
  std::string bytes;
  for (int i = 0; i < 256; ++i) bytes.push_back(static_cast<char>(i));
  std::string encoded = base64Encode(bytes);
  CHECK(encoded.size() == 344);
  CHECK(encoded.compare(0, 8, "AAECAwQF") == 0);
  CHECK(encoded.compare(encoded.size() - 8, 8, "/P3+/w==") == 0);
  CHECK(base64Decode(encoded) == bytes);
  // Decoding stops at the first byte outside the alphabet.
  CHECK(base64Decode("Zm9v!Zm9v") == "foo");
}

void randoms() {
  namespace r = neko::util::random;
  std::string hex = r::randomHex(32);
  CHECK(hex.size() == 32);
  CHECK(hex.find_first_not_of("0123456789abcdef") == std::string::npos);
  CHECK(r::randomHex(32) != hex);
  CHECK(r::randomHex(0).empty());
  for (int i = 0; i < 200; ++i) {
    std::uint64_t n = r::randomNDigitNumber(3, 3);
    CHECK(n >= 100 && n <= 999);
  }
  std::string s = r::generateRandomString(64, "xyz");
  CHECK(s.size() == 64);
  CHECK(s.find_first_not_of("xyz") == std::string::npos);
  CHECK(r::generateRandomString(16).find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") == std::string::npos);
  CHECK(throws<std::invalid_argument>([] { r::generateRandomString(-1); }));
  CHECK(throws<std::invalid_argument>([] { r::generateRandomString(3, ""); }));
}

void checks() {
  namespace c = neko::util::check;
  CHECK(c::isUrl("https://example.com/a/b?c=d"));
  CHECK(c::isUrl("http://sub.example.org"));
  CHECK(!c::isUrl("ftp://example.com"));
  CHECK(!c::isUrl("https://localhost"));
  CHECK(c::isProxyAddress("socks5://127.0.0.1:1080"));
  CHECK(c::isProxyAddress("http://proxy.local:8080"));
  CHECK(!c::isProxyAddress("http://proxy.local"));
  CHECK(!c::isProxyAddress("ftp://host:21"));
  auto res = c::matchResolution("Display 2560X1440@144Hz");
  CHECK(res && res->full == "2560X1440" && res->width == "2560" && res->height == "1440");
  CHECK(!c::matchResolution("1x1"));
}

void uuids() {
  namespace id = neko::util::uuid;
  std::set<std::string> seen;
  for (int i = 0; i < 1000; ++i) {
    std::string u = id::uuidV4();
    bool ok = u.size() == 36 && u[8] == '-' && u[13] == '-' && u[18] == '-' && u[23] == '-' &&
              u[14] == '4' && std::string("89ab").find(u[19]) != std::string::npos;
    for (std::size_t j = 0; ok && j < u.size(); ++j)
      if (j != 8 && j != 13 && j != 18 && j != 23)
        ok = std::string("0123456789abcdef").find(u[j]) != std::string::npos;
    CHECK(ok);
    seen.insert(u);
  }
  CHECK(seen.size() == 1000);

  const std::array<std::uint8_t, 16> dns{0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1,
                                         0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};
  CHECK(id::uuidStringToBytes("6ba7b810-9dad-11d1-80b4-00c04fd430c8") == dns);
  CHECK(id::uuidStringToBytes("6BA7B810-9DAD-11D1-80B4-00C04FD430C8") == dns);
  // Name-based UUIDs need the hash feature, which this port does not build.
  CHECK(throws<neko::ex::NotImplemented>([] { id::uuidV3("example.com"); }));
}

void patterns() {
  namespace p = neko::util::pattern;
  CHECK(p::isPatternDir("logs/") && !p::isPatternDir("logs"));
  CHECK(p::containsWildcard("*.txt") && !p::containsWildcard("a.txt"));
  CHECK(p::isExtensionPattern(".txt") && !p::isExtensionPattern("*.txt"));
  CHECK(p::wildcardToRegexString("a.b*/c") == "a\\.b.*\\/c");
  CHECK(p::matchWildcardPattern("dir/test.txt", "*.txt"));
  CHECK(!p::matchWildcardPattern("dir/test.txt.bak", "*.txt"));
  CHECK(p::matchWildcardPattern("project/src/main.cpp", "src/*.cpp"));
  CHECK(!p::matchWildcardPattern("project/include/main.cpp", "src/*.cpp"));
  CHECK(p::matchWildcardPattern("/opt/src/x.cpp", "/opt/src/*.cpp"));
  CHECK(!p::matchWildcardPattern("/opt/inc/x.cpp", "/opt/src/*.cpp"));

  const std::string target = "/var/app/logs/today.log";
  CHECK(p::matchAny(target, {".log"}));
  CHECK(!p::matchAny(target, {".txt"}));
  CHECK(p::matchAny(target, {"*.log"}));
  CHECK(p::matchAny(target, {"logs/"}));
  CHECK(!p::matchAny(target, {"log/"}));
  CHECK(p::matchAny(target, {"/var/app/"}));
  CHECK(!p::matchAny(target, {"/var/ap/"}));
  CHECK(p::matchAny(target, {"app/logs/today.log"}));
  CHECK(!p::matchAny(target, {"pp/logs/today.log"}));
  CHECK(p::matchAny(target, {"today.log"}));
  CHECK(p::matchAny(target, {"/var/app/logs/today.log"}));
  CHECK(!p::matchAny(target, {"/var/app/logs/other.log"}));
  CHECK(p::matchAny(target, {".txt", "*"}));
  CHECK(!p::matchAny(target, {}));
  CHECK(!p::matchAny(target, {"", "*.txt"}));
}

void fileTypes() {
  namespace d = neko::util::detect;
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / ("neko-function-test-" + neko::util::random::randomHex(12));
  fs::create_directories(dir);
  auto write = [&dir](const char* name, std::string bytes) {
    std::ofstream(dir / name, std::ios::binary) << bytes;
    return (dir / name).string();
  };
  std::string png = write("image.bin", std::string("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16));
  std::string zip = write("report.docx", std::string("PK\x03\x04\x14\0\0\0", 8));
  std::string elf = write("program", std::string("\x7f" "ELF\x02\x01\x01", 7));
  std::string csv = write("table.csv", "a,b\n1,2\n");
  std::string jpg = write("photo.JPG", "zz");
  std::string odd = write("mystery.qqq", "zzzz");

  // The magic number decides before the extension
  // (detectFileType.hpp:225-233), so report.docx is a ZIP.
  CHECK(d::detectFileType(png) == "PNG");
  CHECK(d::detectFileType(zip) == "ZIP");
  CHECK(d::detectFileType(elf) == "ELF");
  CHECK(d::detectFileType(csv) == "CSV");
  CHECK(d::detectFileType(jpg) == "JPEG");
  CHECK(throws<neko::ex::FileError>([&] { d::detectFileType(odd); }));
  CHECK(d::detectFileType(odd, true) == "Unknown");
  std::string missing = (dir / "missing.png").string();
  CHECK(throws<neko::ex::FileError>([&] { d::detectFileType(missing); }));
  CHECK(d::detectFileType(missing, true) == "Unknown");
  CHECK(d::isTargetFileType(png, {"gif", "png"}));
  CHECK(!d::isTargetFileType(png, {"JPEG", "GIF"}));
  CHECK(!d::isTargetFileType(png, {"png"}, true));
  CHECK(d::detail::typeByExtension("7z") == "7Z");

  std::error_code ec;
  fs::remove_all(dir, ec);
}

}  // namespace

int main() {
  setTimeZone();
  operators();
  strings();
  memoryAndLambdas();
  logicAndMath();
  times();
  base64();
  randoms();
  checks();
  uuids();
  patterns();
  fileTypes();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("neko-function: ok");
  return 0;
}
