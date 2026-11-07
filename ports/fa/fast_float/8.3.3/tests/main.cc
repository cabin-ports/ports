// Parses decimal strings whose correctly rounded binary values are known
// (including round-half-to-even ties and a classic hard case), integers,
// a comma decimal separator, and malformed input.  C++11, the interface
// standard the port declares.
#include <fast_float/fast_float.h>

#include <cstdio>
#include <cstring>
#include <system_error>

static bool fail(const char* what) {
    std::fprintf(stderr, "fast_float test failed: %s\n", what);
    return false;
}

template <typename T>
static bool parses(const char* text, T expected) {
    T value = 0;
    const char* last = text + std::strlen(text);
    const fast_float::from_chars_result r = fast_float::from_chars(text, last, value);
    return (r.ec == std::errc() && r.ptr == last && value == expected) || fail(text);
}

int main() {
    bool ok = parses("3.1416", 3.1416) && parses("0.1", 0.1) && parses("1e23", 1e23) &&
              parses("-2.2250738585072014e-308", -2.2250738585072014e-308) &&
              // 2^53 + 1 is a tie between 2^53 and 2^53 + 2: rounds to even.
              parses("9007199254740993", 9007199254740992.0) &&
              // 2^24 + 1 ties the same way in single precision.
              parses("16777217", 16777216.0f) && parses("-42", -42) &&
              parses("18446744073709551615", 18446744073709551615ull);

    double value = 0;
    const char comma[] = "2,5";
    const fast_float::parse_options options(fast_float::chars_format::general, ',');
    const fast_float::from_chars_result r =
        fast_float::from_chars_advanced(comma, comma + 3, value, options);
    ok = ok && ((r.ec == std::errc() && r.ptr == comma + 3 && value == 2.5) || fail("2,5"));

    // Only the leading number is consumed.
    const char trailing[] = "1.5e3xyz";
    const fast_float::from_chars_result t = fast_float::from_chars(trailing, trailing + 8, value);
    ok = ok && ((t.ec == std::errc() && t.ptr == trailing + 5 && value == 1500.0) ||
                fail("trailing"));

    const char bad[] = "x1";
    const fast_float::from_chars_result b = fast_float::from_chars(bad, bad + 2, value);
    ok = ok && ((b.ec == std::errc::invalid_argument && b.ptr == bad) || fail("invalid input"));

    int narrow = 0;
    const char big[] = "99999999999";
    const fast_float::from_chars_result o = fast_float::from_chars(big, big + 11, narrow);
    ok = ok && (o.ec == std::errc::result_out_of_range || fail("int overflow"));
    return ok ? 0 : 1;
}
