// Consumer test: EcmaScript-style shortest, fixed, exponential and
// precision formatting, parsing with processed-character counts, junk,
// special values and separators, and shortest-form round trips.
#include <double-conversion/double-conversion.h>

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <limits>

using double_conversion::DoubleToStringConverter;
using double_conversion::StringBuilder;
using double_conversion::StringToDoubleConverter;

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

enum Mode { SHORTEST, SHORTEST_SINGLE, FIXED, EXPONENTIAL, PRECISION };

void expect(Mode mode, double value, int digits, const char* want, int line) {
  const DoubleToStringConverter& conv =
      DoubleToStringConverter::EcmaScriptConverter();
  char buffer[128];
  StringBuilder builder(buffer, sizeof buffer);
  bool ok = false;
  switch (mode) {
    case SHORTEST: ok = conv.ToShortest(value, &builder); break;
    case SHORTEST_SINGLE:
      ok = conv.ToShortestSingle(static_cast<float>(value), &builder);
      break;
    case FIXED: ok = conv.ToFixed(value, digits, &builder); break;
    case EXPONENTIAL: ok = conv.ToExponential(value, digits, &builder); break;
    case PRECISION: ok = conv.ToPrecision(value, digits, &builder); break;
  }
  const char* got = builder.Finalize();
  if (!ok || std::strcmp(got, want) != 0) {
    std::fprintf(stderr, "main.cc:%d: got \"%s\" (ok=%d), want \"%s\"\n", line,
                 got, ok ? 1 : 0, want);
    ++failures;
  }
}
#define EXPECT(mode, value, digits, want) \
  expect((mode), (value), (digits), (want), __LINE__)

double parse(const StringToDoubleConverter& conv, const char* text,
             int* processed) {
  return conv.StringToDouble(text, static_cast<int>(std::strlen(text)),
                             processed);
}

}  // namespace

int main() {
  EXPECT(SHORTEST, 0.1, 0, "0.1");
  EXPECT(SHORTEST, 0.1 + 0.2, 0, "0.30000000000000004");
  EXPECT(SHORTEST, 1e20, 0, "100000000000000000000");
  EXPECT(SHORTEST, 1e21, 0, "1e+21");
  EXPECT(SHORTEST, -0.0, 0, "0");
  EXPECT(SHORTEST, 5e-324, 0, "5e-324");
  EXPECT(SHORTEST, DBL_MAX, 0, "1.7976931348623157e+308");
  EXPECT(SHORTEST, std::numeric_limits<double>::infinity(), 0, "Infinity");
  EXPECT(SHORTEST_SINGLE, 0.1, 0, "0.1");
  EXPECT(FIXED, 3.14159, 2, "3.14");
  EXPECT(FIXED, 1.005, 2, "1.00");  // 1.005 is stored as 1.00499999...
  EXPECT(EXPONENTIAL, 1234.5678, 3, "1.235e+3");
  EXPECT(PRECISION, 0.000123456, 3, "0.000123");
  EXPECT(PRECISION, 123456.0, 2, "1.2e+5");

  const double nan = std::numeric_limits<double>::quiet_NaN();
  StringToDoubleConverter lenient(
      StringToDoubleConverter::ALLOW_TRAILING_JUNK |
          StringToDoubleConverter::ALLOW_LEADING_SPACES,
      0.0, nan, "Infinity", "NaN");
  int processed = -1;
  CHECK(parse(lenient, "  1.5e3xyz", &processed) == 1500.0);
  CHECK(processed == 7);
  CHECK(parse(lenient, "0.1", &processed) == 0.1);
  CHECK(parse(lenient, "2.2250738585072011e-308", &processed) ==
        2.2250738585072011e-308);
  // Halfway between two doubles the fast path cannot decide and the
  // bignum comparison does: a tie rounds to even, anything above it up.
  CHECK(parse(lenient, "9007199254740993", &processed) == 9007199254740992.0);
  CHECK(parse(lenient, "9007199254740993.000000000000000000000001",
              &processed) == 9007199254740994.0);
  CHECK(parse(lenient, "-Infinity", &processed) ==
        -std::numeric_limits<double>::infinity());
  CHECK(parse(lenient, "", &processed) == 0.0);
  float f = lenient.StringToFloat("3.4028235e38", 12, &processed);
  CHECK(f == FLT_MAX);

  StringToDoubleConverter strict(StringToDoubleConverter::NO_FLAGS, 0.0, nan,
                                 "Infinity", "NaN");
  double junk = parse(strict, "12abc", &processed);
  CHECK(junk != junk);

  StringToDoubleConverter grouped(StringToDoubleConverter::NO_FLAGS, 0.0, nan,
                                  NULL, NULL, '\'');
  CHECK(parse(grouped, "1'000'000.5", &processed) == 1000000.5);
  CHECK(processed == 11);

  // Shortest output must parse back to the identical double.
  const double samples[] = {1.0 / 3.0, 3.141592653589793, 123.456e-200,
                            9007199254740994.0, 4.35, 1e-7};
  for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
    char buffer[64];
    StringBuilder builder(buffer, sizeof buffer);
    CHECK(DoubleToStringConverter::EcmaScriptConverter().ToShortest(
        samples[i], &builder));
    const char* text = builder.Finalize();
    CHECK(parse(strict, text, &processed) == samples[i]);
    CHECK(processed == static_cast<int>(std::strlen(text)));
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("double-conversion consumer test passed");
  return 0;
}
