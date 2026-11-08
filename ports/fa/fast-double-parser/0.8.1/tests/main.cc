// fast_double_parser from C++11: parse_number on RFC 7159 numbers, checked
// bit for bit against correctly rounded binary64 values on each of its three
// paths (Clinger's exact arithmetic, Eisel-Lemire, and the C locale's strtod
// fallback), the end pointer it returns, and the inputs the grammar rejects.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <fast_double_parser.h>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

std::uint64_t bits(double value) {
  std::uint64_t out;
  std::memcpy(&out, &value, sizeof out);
  return out;
}

// Parses text, which must be consumed up to its terminating character.
bool parses_to(const char* text, std::uint64_t expected) {
  double value = 1.0;
  const char* end = fast_double_parser::parse_number(text, &value);
  if (end == nullptr || *end != '\0') {
    std::fprintf(stderr, "'%s' was not parsed whole\n", text);
    return false;
  }
  if (bits(value) != expected) {
    std::fprintf(stderr, "'%s' -> %.17g\n", text, value);
    return false;
  }
  return true;
}

bool rejects(const char* text) {
  double value = 0.0;
  return fast_double_parser::parse_number(text, &value) == nullptr;
}

}  // namespace

int main() {
  // Clinger: a significand below 2^53 and a power of ten up to 22.
  CHECK(parses_to("0.1", 0x3fb999999999999aULL));
  CHECK(parses_to("7e22", 0x44ada56a4b0835c0ULL));
  CHECK(parses_to("-2.5e-3", 0xbf647ae147ae147bULL));
  CHECK(parses_to("-0", 0x8000000000000000ULL));
  CHECK(parses_to("0", 0x0000000000000000ULL));
  CHECK(parses_to("1E+2", 0x4059000000000000ULL));

  // Eisel-Lemire: up to 19 significant digits, exponents in [-325, 308].
  CHECK(parses_to("0.30000000000000004", 0x3fd3333333333334ULL));
  CHECK(parses_to("2.2250738585072014e-308", 0x0010000000000000ULL));
  CHECK(parses_to("1.7976931348623157e308", 0x7fefffffffffffffULL));
  CHECK(parses_to("1e-300", 0x01a56e1fc2f8f359ULL));
  // The 128-bit refinement of the product, and a mantissa that rounds up to
  // 2^53.
  CHECK(parses_to("605e-30", 0x3a47f76dccb9957aULL));
  CHECK(parses_to("7.2057594037927933e+16", 0x4370000000000000ULL));

  // strtod: too close to a halfway point for Eisel-Lemire, 19 or more
  // significant digits, or a subnormal result.
  CHECK(parses_to("1e23", 0x44b52d02c7e14af6ULL));
  CHECK(parses_to("-1e23", 0xc4b52d02c7e14af6ULL));
  CHECK(parses_to("9007199254740993", 0x4340000000000000ULL));  // ties to even
  CHECK(parses_to("3.141592653589793238462643383279502884197",
                  0x400921fb54442d18ULL));
  CHECK(parses_to("123456789012345678901234567890", 0x45f8ee90ff6c373eULL));
  CHECK(parses_to("4.9e-324", 0x0000000000000001ULL));

  // The end pointer stops at the first character after the number.
  {
    const char text[] = "12.5e2, 7";
    double value = 0.0;
    const char* end = fast_double_parser::parse_number(text, &value);
    CHECK(end == text + 6);
    CHECK(value == 1250.0);
  }

  // RFC 7159 grammar, and no infinities or NaNs.
  CHECK(rejects("1e309"));
  CHECK(rejects("-1e400"));
  CHECK(rejects("nan"));
  CHECK(rejects("inf"));
  CHECK(rejects("+1"));
  CHECK(rejects(".5"));
  CHECK(rejects("01"));
  CHECK(rejects("1."));
  CHECK(rejects("1e"));
  CHECK(rejects("-"));
  CHECK(rejects(""));
  CHECK(rejects(" 1"));

  if (failures != 0) {
    return 1;
  }
  std::printf("fast_double_parser 0.8.1: C++ API OK\n");
  return 0;
}
