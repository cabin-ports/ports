// Expected values are exact decimal arithmetic worked by hand; the rounding rows follow
// README.md's "Supported rounding modes".
#include <decimal.h>

#include <cstdio>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

void checkStr(const std::string &got, const char *want, int line) {
    if (got != want) {
        std::fprintf(stderr, "main.cc:%d: got \"%s\", want \"%s\"\n", line, got.c_str(), want);
        ++failures;
    }
}
#define CHECK_STR(got, want) checkStr((got), (want), __LINE__)

using dec::decimal;
using dec::decimal_cast;
using dec::toString;

void testArithmetic() {
    // 0.1 + 0.2 is not 0.3 in binary floating point.
    CHECK(decimal_cast<2>("0.10") + decimal_cast<2>("0.20") == decimal_cast<2>("0.30"));
    const decimal<2> price("19.99");
    CHECK_STR(toString(price * 3), "59.97");
    CHECK_STR(toString(price - decimal_cast<2>("20")), "-0.01");
    // decimal.h's sample: 143125 / 333 = 429.8048...
    CHECK_STR(toString(decimal<2>(143125) / decimal_cast<2>(333)), "429.80");
    // The raw product 123456789012 * 1000500000 does not fit in int64.
    CHECK_STR(toString(decimal_cast<6>("123456.789012") * decimal_cast<6>("1000.5")), "123518517.406506");
    CHECK(decimal_cast<2>("7.50") % decimal_cast<2>("2") == decimal_cast<2>("1.50"));
    CHECK(decimal_cast<2>("-3.75").abs() == decimal_cast<2>("3.75"));
    CHECK(decimal_cast<2>("2.5") > 2 && decimal_cast<2>("2.5") < 3 && decimal_cast<2>("-0.01").sign() == -1);

    // Ties: 1.375 and 0.125 round half away from zero by default; 0.125 and 0.875 round to even
    // under half_even_round_policy.
    using Even = decimal<2, dec::half_even_round_policy>;
    CHECK_STR(toString(decimal_cast<2>("1.25") * decimal_cast<2>("1.10")), "1.38");
    CHECK_STR(toString(decimal_cast<2>("1.00") / 8), "0.13");
    CHECK_STR(toString(Even("1.00") / Even(8)), "0.12");
    CHECK_STR(toString(Even("3.50") / Even(4)), "0.88");
}

template <typename Policy>
std::string roundRow() {
    const char *const inputs[] = {"2.5", "3.5", "-2.5", "2.4", "2.6", "-2.6"};
    std::string row;
    for (const char *s : inputs) {
        if (!row.empty()) {
            row += ' ';
        }
        row += toString(decimal<0, Policy>(std::string(s)));
    }
    return row;
}

void testRoundingPolicies() {
    CHECK_STR(roundRow<dec::def_round_policy>(), "3 4 -3 2 3 -3");
    CHECK_STR(roundRow<dec::half_up_round_policy>(), "3 4 -2 2 3 -3");
    CHECK_STR(roundRow<dec::half_down_round_policy>(), "2 3 -3 2 3 -3");
    CHECK_STR(roundRow<dec::half_even_round_policy>(), "2 4 -2 2 3 -3");
    CHECK_STR(roundRow<dec::ceiling_round_policy>(), "3 4 -2 3 3 -2");
    CHECK_STR(roundRow<dec::floor_round_policy>(), "2 3 -3 2 2 -3");
    CHECK_STR(roundRow<dec::round_down_round_policy>(), "2 3 -2 2 2 -2");
    CHECK_STR(roundRow<dec::round_up_round_policy>(), "3 4 -3 3 3 -3");

    const decimal<2> v = decimal_cast<2>("-2.50");
    CHECK(v.getAsInteger() == -3);
    CHECK_STR(toString(v.round()), "-3.00");
    CHECK_STR(toString(v.floor()), "-3.00");
    CHECK_STR(toString(v.ceil()), "-2.00");
    CHECK_STR(toString(v.trunc()), "-2.00");
}

void testPrecisionAndParts() {
    CHECK_STR(toString(decimal_cast<2>(decimal_cast<4>("1.2345"))), "1.23");
    CHECK_STR(toString(decimal_cast<2>(decimal_cast<4>("1.2350"))), "1.24");
    decimal<2> narrowed;
    narrowed = decimal_cast<4>("-1.2350");
    CHECK_STR(toString(narrowed), "-1.24");

    dec::int64 before = 0, after = 0;
    decimal_cast<2>("-7.05").unpack(before, after);
    CHECK(before == -7 && after == -5);
    CHECK_STR(toString(decimal<2>().pack(12, 34)), "12.34");

    CHECK_STR(toString(decimal<4>::buildWithExponent(12345, -2)), "123.4500");
    dec::int64 mantissa = 0;
    int exponent = 0;
    decimal_cast<2>("1.50").getWithExponent(mantissa, exponent);
    CHECK(mantissa == 15 && exponent == -1);
    CHECK(decimal_cast<3>("0.007").getUnbiased() == 7);
}

void testText() {
    const dec::decimal_format european(',', '.');
    CHECK_STR(toString(decimal_cast<2>("1234567.89"), european), "1.234.567,89");
    CHECK(dec::fromString<decimal<2>>("1.234.567,89", european) == decimal_cast<2>("1234567.89"));

    std::ostringstream os;
    os << decimal_cast<2>("-12.3") << '|' << decimal_cast<3>("0.007");
    CHECK_STR(os.str(), "-12.30|0.007");

    // Leading blanks are skipped and extra digits are rounded: -0.125 -> -0.13.
    std::istringstream is("  -0.125 7");
    decimal<2> d;
    int n = 0;
    is >> d >> n;
    CHECK_STR(toString(d), "-0.13");
    CHECK(n == 7);

    std::istringstream bad("x1");
    decimal<2> e = decimal_cast<2>("5");
    CHECK(!dec::fromStream(bad, e) && e == 0);
}

}  // namespace

int main() {
    testArithmetic();
    testRoundingPolicies();
    testPrecisionAndParts();
    testText();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("decimal_for_cpp: ok\n");
    return 0;
}
