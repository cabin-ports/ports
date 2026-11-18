// Classifies the README examples and the int64 boundaries, comparing each
// ScalarKind exactly, and checks the values output_refs stores.

#include <classify_scalar.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace classify_scalar;

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

template <std::size_t N> static void expect(const char (&text)[N], ScalarKind kind) {
  const ScalarKind actual = classify_scalar::classify_scalar(text);
  if (actual != kind) {
    std::fprintf(stderr, "FAIL: classify_scalar(\"%s\") is %d, not %d\n", text,
                 static_cast<int>(actual), static_cast<int>(kind));
    ++failures;
  }
}

int main() {
  // README.md "Built-In Examples".
  expect("hello", scalar_string);
  expect("", scalar_null);
  expect("   ", scalar_null);
  expect("true", scalar_bool);
  expect("FALSE", scalar_bool);
  expect("42", scalar_int8);
  expect("-17", scalar_int8);
  expect("0x2a", scalar_int8);
  expect("9223372036854775808", scalar_bigint);
  expect("3.14159", scalar_float);
  expect("-1.25e2", scalar_int8);
  expect("1e-3", scalar_float);
  expect("2024-01-31", scalar_timestamp);
  expect("2024-01-31T23:59:58Z", scalar_timestamp);
  expect("2021-04-05T10:14:57-0600", scalar_timestamp);
  expect("  42  ", scalar_int8);
  check(classify_scalar::classify_scalar<ScalarKind, false>("  42  ") == scalar_string,
        "untrimmed input is a string");
  const char *span = "-1.5,rest";
  check(classify_scalar::classify_scalar(span, span + std::strlen("-1.5")) == scalar_float,
        "pointer span");

  // Integer widths and the int64 limits.
  expect("300", scalar_int16);
  expect("70000", scalar_int32);
  expect("9223372036854775807", scalar_int64);
  expect("-9223372036854775808", scalar_int64);
  expect("-9223372036854775809", scalar_bigint);

  std::int64_t integer = 0;
  long double number = 0;
  bool boolean = false;
  check(classify_scalar::classify_scalar("  -0x2a  ", output_refs(number, integer, boolean)) ==
                scalar_int8 &&
            integer == -42 && number == 0,
        "output_refs stores a hex integer");
  check(classify_scalar::classify_scalar("2.5", output_refs(number, integer, boolean)) ==
                scalar_float &&
            number == 2.5L,
        "output_refs stores a float");
  check(classify_scalar::classify_scalar("TRUE", output_refs(number, integer, boolean)) ==
                scalar_bool &&
            boolean,
        "output_refs stores a bool");

  // Timestamps are stored as UTC milliseconds since the Unix epoch.
  std::uint64_t timestamp = 0;
  builtin_output_refs with_timestamp(number, integer, boolean, timestamp);
  check(classify_scalar::classify_scalar("2024-01-31T23:59:58Z", with_timestamp) ==
                scalar_timestamp &&
            timestamp == UINT64_C(1706745598000),
        "UTC timestamp value");
  check(classify_scalar::classify_scalar("2021-04-05T10:14:57-0600", with_timestamp) ==
                scalar_timestamp &&
            timestamp == UINT64_C(1617639297000),
        "timestamp with a -0600 offset");

  // 2^63 written as a float is outside int64; the largest double below it and
  // -2^63 are integral.
  expect("9223372036854775808.0", scalar_float);
  check(classify_scalar::classify_scalar("9223372036854774784.0",
                                         output_refs(number, integer, boolean)) == scalar_int64 &&
            integer == INT64_C(9223372036854774784),
        "largest integral double below 2^63");
  check(classify_scalar::classify_scalar("-9223372036854775808.0",
                                         output_refs(number, integer, boolean)) == scalar_int64 &&
            integer == std::numeric_limits<std::int64_t>::min(),
        "-2^63 as a float");
  expect("-9223372036854777856.0", scalar_float);

#ifdef CLASSIFY_SCALAR_HAS_CXX17
  check(classify_scalar::classify_scalar(std::string_view{"42"}) == scalar_int8,
        "string_view overload");
#endif

  if (failures != 0) {
    return 1;
  }
  std::puts("vincentlaucsb-classify-scalar 1.1.2 C++ ok");
  return 0;
}
