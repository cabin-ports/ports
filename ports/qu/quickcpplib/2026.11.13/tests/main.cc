// Exercises span, scope, FNV-1a, SHA-256 and hex conversion.
#include <quickcpplib/algorithm/hash.hpp>
#include <quickcpplib/algorithm/string.hpp>
#include <quickcpplib/scope.hpp>
#include <quickcpplib/span.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace q = QUICKCPPLIB_NAMESPACE;

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main() {
  // span over a vector (span.hpp:105 nonstd::span before C++20).
  std::vector<int> v{1, 2, 3};
  q::span::span<int> s(v);
  CHECK(s.size() == 3);
  CHECK(s[0] == 1 && s[2] == 3);
  s[1] = 20;
  CHECK(v[1] == 20);

  // scope_exit takes a noexcept function (scope.hpp:150-153) and runs it on
  // leaving the scope, not before.
  int n = 0;
  {
    auto guard = q::scope::make_scope_exit([&]() noexcept { n = 42; });
    CHECK(n == 0);
  }
  CHECK(n == 42);

  // FNV-1a test vectors from the FNV reference test_fnv.c: "a" and "foobar".
  // The word size follows the pointer size (hash.hpp:50-56).
  using fnv = q::algorithm::hash::fnv1a_hash<char>;
  CHECK(fnv()('a') == (sizeof(void *) == 8 ? size_t(0xaf63dc4c8601ec8cULL)
                                           : size_t(0xe40c292cU)));
  auto h = fnv::begin();
  for (char c : std::string("foobar"))
    fnv::add(h, c);
  CHECK(h == (sizeof(void *) == 8 ? fnv::working_type(0x85944171f73967e8ULL)
                                  : fnv::working_type(0xbf9cf968U)));

  // SHA-256 of "abc" (FIPS 180-2 example), rendered through to_hex_string.
  const unsigned char abc[] = {'a', 'b', 'c'};
  auto d = q::algorithm::hash::sha256_hash::hash(abc, 3);
  CHECK(q::algorithm::string::to_hex_string(d.as_bytes, 32) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  // to_hex_string over a pointer and over a span, odd length included.
  const unsigned char b[] = {0xde, 0xad, 0xbe, 0xef};
  CHECK(q::algorithm::string::to_hex_string(b, 4) == "deadbeef");
  CHECK(q::algorithm::string::to_hex_string(b, 3) == "deadbe");
  CHECK(q::algorithm::string::to_hex_string(
            q::span::span<const unsigned char>(b, 4)) == "deadbeef");

  if (failures == 0)
    std::puts("ok");
  return failures == 0 ? 0 : 1;
}
