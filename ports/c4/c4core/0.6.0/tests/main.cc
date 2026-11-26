#include <c4/base64.hpp>
#include <c4/charconv.hpp>
#include <c4/format.hpp>
#include <c4/substr.hpp>
#include <c4/version.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,  \
                   #cond);                                                   \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

int main() {
  CHECK(std::strcmp(c4::version(), "0.6.0") == 0);

  // csubstr: find, trim and split.
  c4::csubstr s = "  hello world  ";
  CHECK(s.trim(' ') == "hello world");
  c4::csubstr t = s.trim(' ');
  CHECK(t.find("world") == 6);
  CHECK(t.find("xyz") == c4::csubstr::npos);
  CHECK(t.first_of('o') == 4);
  CHECK(t.begins_with("hello") && t.ends_with("world"));
  CHECK(t.sub(0, 5) == "hello");
  CHECK(t.right_of(t.find(' '), false) == "world");

  c4::csubstr csv = "a,bc,,def";
  const char *expected[] = {"a", "bc", "", "def"};
  size_t n = 0;
  for (c4::csubstr tok : csv.split(',')) {
    CHECK(n < 4 && tok == expected[n]);
    ++n;
  }
  CHECK(n == 4);

  size_t pos = 0, commas = 0;
  while ((pos = csv.first_of(',', pos)) != c4::csubstr::npos) {
    ++commas;
    ++pos;
  }
  CHECK(commas == 3);

  // Integer and float conversions; atod goes through fast_float.
  int i = 0;
  CHECK(c4::atoi(c4::csubstr("1234"), &i) && i == 1234);
  CHECK(c4::atoi(c4::csubstr("-42"), &i) && i == -42);
  CHECK(!c4::atoi(c4::csubstr("abc"), &i));
  double d = 0;
  CHECK(c4::atod(c4::csubstr("3.25"), &d) && d == 3.25);
  float f = 0;
  CHECK(c4::atof(c4::csubstr("-0.5e2"), &f) && f == -50.0f);

  char buf[64];
  c4::substr b(buf, sizeof buf);
  size_t len = c4::to_chars(b, int32_t(-42));
  CHECK(len == 3 && c4::csubstr(buf, len) == "-42");
  int32_t back = 0;
  CHECK(c4::from_chars(c4::csubstr(buf, len), &back) && back == -42);

  double v = 0.1 + 0.2;
  // 17 significant digits round-trip any double; the default precision need not.
  len = c4::dtoa(b, v, 17);
  CHECK(c4::csubstr(buf, len) == "0.30000000000000004");
  double v2 = 0;
  CHECK(c4::from_chars(c4::csubstr(buf, len), &v2) && v2 == v);
  float fv = 1.5f;
  len = c4::to_chars(b, fv);
  CHECK(c4::csubstr(buf, len) == "1.5");
  float fv2 = 0;
  CHECK(c4::from_chars(c4::csubstr(buf, len), &fv2) && fv2 == fv);

  // format and cat.
  len = c4::format(b, "the {} drank {} {}", "partier", 5, "beers");
  CHECK(c4::csubstr(buf, len) == "the partier drank 5 beers");
  len = c4::cat(b, "x=", 7, ",y=", 2.5);
  CHECK(c4::csubstr(buf, len) == "x=7,y=2.5");
  // A too-small buffer reports the required size without overflowing.
  char small[4];
  CHECK(c4::cat(c4::substr(small, sizeof small), "abcdefgh") == 8);

  // base64.
  char dec[32] = {};
  size_t required = 0;
  char enc2[32] = {};
  len = c4::base64_encode(enc2, sizeof enc2, "Man is distinguished", 20);
  CHECK(len == 28 && c4::csubstr(enc2, len) == "TWFuIGlzIGRpc3Rpbmd1aXNoZWQ=");
  CHECK(c4::base64_decode(enc2, len, dec, sizeof dec, &required));
  CHECK(required == 20 && c4::csubstr(dec, required) == "Man is distinguished");

  return failures == 0 ? 0 : 1;
}
