#include <SafeInt.hpp>

#include <climits>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The code of the SafeIntException op throws, or SafeIntNoError.
template <typename F> SafeIntError thrown(F op)
{
    try {
        op();
    } catch (const SafeIntException &e) {
        return e.m_code;
    }
    return SafeIntNoError;
}

void test_class()
{
    SafeInt<int> a(INT_MAX - 1);
    a += 1;
    check(a == INT_MAX, "SafeInt<int> reaches INT_MAX");
    check(thrown([&] { a += 1; }) == SafeIntArithmeticOverflow, "SafeInt<int> overflow throws");
    check(thrown([] { SafeInt<int> x(INT_MIN); x /= -1; }) == SafeIntArithmeticOverflow,
          "INT_MIN / -1 throws overflow");
    check(thrown([] { SafeInt<int> x(1); x /= 0; }) == SafeIntDivideByZero, "division by zero throws its own code");
    check(thrown([] { SafeInt<int> x(1); x %= 0; }) == SafeIntDivideByZero, "modulus by zero throws its own code");

    SafeInt<unsigned int> u(5u);
    u -= 3;
    check(u == 2u, "SafeInt<unsigned> subtraction");
    check(thrown([&] { u -= 3; }) == SafeIntArithmeticOverflow, "unsigned underflow throws");

    SafeInt<unsigned char> c(200);
    c += 55;
    check(c == 255, "SafeInt<unsigned char> takes an int operand");
    check(thrown([&] { c += 1; }) == SafeIntArithmeticOverflow, "SafeInt<unsigned char> overflow throws");

    check(thrown([] { SafeInt<short> s(70000); (void)s; }) == SafeIntArithmeticOverflow,
          "constructing from an out-of-range value throws");
    check(thrown([] {
              SafeInt<int> negative(-1);
              unsigned int converted = negative;
              (void)converted;
          }) == SafeIntArithmeticOverflow,
          "converting a negative SafeInt<int> to unsigned throws");

    SafeInt<unsigned long long> big(0x100000000ULL);
    check(thrown([&] { big *= 0x100000000ULL; }) == SafeIntArithmeticOverflow, "64-bit multiplication overflow throws");

    check(SafeInt<int>(-1) < 1u, "SafeInt compares signed and unsigned by value");
}

void test_functions()
{
    int i = 0;
    check(SafeMultiply(1000, 1000, i) && i == 1000000, "SafeMultiply product");
    check(!SafeMultiply(46341, 46341, i), "SafeMultiply overflow");
    check(!SafeAdd(INT_MAX, 1, i), "SafeAdd overflow");
    check(SafeDivide(-7, 2, i) && i == -3, "SafeDivide quotient");
    check(!SafeDivide(1, 0, i), "SafeDivide by zero");
    check(SafeModulus(-7, 3, i) && i == -1, "SafeModulus remainder");
    check(!SafeNegation(INT_MIN, i), "SafeNegation of INT_MIN");

    unsigned int u = 0;
    check(!SafeSubtract(0u, 1, u), "SafeSubtract unsigned underflow");

    unsigned long long u64 = 0;
    check(SafeMultiply(0x100000000ULL, 0xFFFFFFFFULL, u64) && u64 == 0xFFFFFFFF00000000ULL,
          "SafeMultiply 64-bit product");

    signed char sc = 0;
    unsigned char uc = 0;
    unsigned int ui = 0;
    check(SafeCast(255, uc) && uc == 255, "SafeCast in range");
    check(!SafeCast(300, sc), "SafeCast above the range");
    check(!SafeCast(-1, ui), "SafeCast of a negative value to unsigned");

    check(SafeLessThan(-1, 1u) && SafeLessThanEquals(-1, 0u), "SafeLessThan compares by value");
    check(!SafeEquals(-1, UINT_MAX) && SafeEquals(255, static_cast<unsigned char>(255)), "SafeEquals by value");
}

} // namespace

int main()
{
    test_class();
    test_functions();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("safeint 3.0.28 C++ ok");
    return 0;
}
