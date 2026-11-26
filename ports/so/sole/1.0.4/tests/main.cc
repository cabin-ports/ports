#include <sole.hpp>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

}  // namespace

int main() {
  const std::string text = "f81d4fae-7dec-11d0-a765-00a0c91e6bf6";
  sole::uuid fixed = sole::rebuild(text);
  check(fixed.ab == 0xf81d4fae7dec11d0ULL, "rebuild ab");
  check(fixed.cd == 0xa76500a0c91e6bf6ULL, "rebuild cd");
  check(fixed.str() == text, "str round trip of a fixed uuid");

  sole::uuid u = sole::uuid4();
  check(sole::rebuild(u.str()) == u, "str round trip of uuid4");
  check(sole::rebuild(u.base62()) == u, "base62 round trip of uuid4");

  std::string s = u.str();
  check(s.size() == 36, "uuid4 length");
  check(s[14] == '4', "uuid4 version digit");
  check(std::string("89ab").find(s[19]) != std::string::npos,
        "uuid4 variant digit");
  check(sole::uuid4() != sole::uuid4(), "uuid4 values differ");

  std::string s1 = sole::uuid1().str();
  check(s1.size() == 36 && s1[14] == '1', "uuid1 version digit");
  std::string s0 = sole::uuid0().str();
  check(s0.size() == 36 && s0[14] == '0', "uuid0 version digit");

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
