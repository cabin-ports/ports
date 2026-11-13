// Expected values come from Python's integers, not from intx.  The operands
// are parsed at run time, so the checks in main() take the compiler-specific
// paths: unsigned __int128 and the carry builtins on GCC and Clang, _umul128
// on x64 cl.  The static_asserts are constant-evaluated: addc and subc take
// their portable code everywhere, umul only where __int128 is missing (cl).
#include <intx/intx.hpp>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

using namespace intx::literals;
using intx::uint128;
using intx::uint256;
using intx::uint512;

int failures = 0;

void check(bool ok, const char *what, int line) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, what);
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

template <unsigned N>
void check_hex(const intx::uint<N> &got, const char *want, int line) {
  const std::string s = intx::hex(got);
  if (s != want) {
    ++failures;
    std::fprintf(stderr, "line %d: got %s, want %s\n", line, s.c_str(), want);
  }
}
#define CHECK_HEX(x, want) check_hex((x), (want), __LINE__)

template <typename T> T parse(const char *s) { return intx::from_string<T>(s); }

template <typename E, typename T> bool throws(const char *s) {
  try {
    (void)intx::from_string<T>(s);
  } catch (const E &) {
    return true;
  }
  return false;
}

constexpr std::uint64_t ones = ~std::uint64_t{0};

// The 64-bit primitives under constant evaluation.
static_assert(intx::umul(ones, ones) == uint128{1, ones - 1});
static_assert(intx::addc(ones, 2).value == 1 && intx::addc(ones, 2).carry);
static_assert(intx::subc(0, 1).value == ones && intx::subc(0, 1).carry);
static_assert(intx::addc(ones, 0, true).value == 0 &&
              intx::addc(ones, 0, true).carry);
static_assert(intx::subc(0, 0, true).value == ones &&
              intx::subc(0, 0, true).carry);

// secp256k1's field prime and its generator's coordinates, as consteval
// literals in both bases.
constexpr auto P =
    0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f_u256;
constexpr auto GX =
    0x79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798_u256;
constexpr auto GY =
    0x483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8_u256;
static_assert(
    P ==
    115792089237316195423570985008687907853269984665640564039457584007908834671663_u256);
static_assert(
    intx::mulmod(GX, GY, P) ==
    0xfd3dc529c6eb60fb9d166034cf3c1a5a72324aa9dfd3428a56d7e1ce0179fd9b_u256);
static_assert(0xfedcba98765432100123456789abcdef_u128 / 7 ==
              0x2468acf13579be02494e2e7c8161afb4_u128);

void test128() {
  const auto a = parse<uint128>("0xfedcba98765432100123456789abcdef");
  const auto b = parse<uint128>("0x1ffffffffffffffff");
  const auto max = parse<uint128>("0xffffffffffffffffffffffffffffffff");

  CHECK_HEX(a + b, "fedcba98765432120123456789abcdee");
  CHECK_HEX(a - b, "fedcba987654320e0123456789abcdf0");
  CHECK_HEX(b - a, "123456789abcdf1fedcba9876543210");
  CHECK_HEX(a * b, "369d0369d0369cdfedcba9876543211");
  CHECK_HEX(max + 1, "0");
  CHECK_HEX(uint128{0} - 1, "ffffffffffffffffffffffffffffffff");
  CHECK(intx::addc(max, uint128{1}).carry);
  CHECK(!intx::addc(a, b).carry);

  // 64 x 64 -> 128.
  CHECK_HEX(intx::umul(max[0], max[1]), "fffffffffffffffe0000000000000001");
  CHECK_HEX(intx::umul(a[0], a[1]), "121fa00ad77d7422236d88fe5618cf0");

  // Division by one word (shifted and already normalized), by two words, by
  // a divisor with the top bit set, and by a larger divisor.
  CHECK_HEX(a / 7, "2468acf13579be02494e2e7c8161afb4");
  CHECK_HEX(a % 7, "3");
  const uint128 d1 = 0x8000000000000001;
  CHECK_HEX(a / d1, "1fdb97530eca8641c");
  CHECK_HEX(a % d1, "369d0369d0369d3");
  CHECK_HEX(a / b, "7f6e5d4c3b2a1908");
  CHECK_HEX(a % b, "8091a2b3c4d5e6f7");
  const auto d2 = (uint128{1} << 127) + 1;
  CHECK_HEX(a / d2, "1");
  CHECK_HEX(a % d2, "7edcba98765432100123456789abcdee");
  CHECK_HEX(a / a, "1");
  CHECK_HEX(a % a, "0");
  // Reaches udivrem_2by1's second correction.
  const auto c1 = parse<uint128>("0x6ceb78814b31b4eaffa506027bc707e1");
  const uint128 c1d = 0x848c86b6b42812e3;
  CHECK_HEX(c1 / c1d, "d25d15249af143b9");
  CHECK_HEX(c1 % c1d, "2cfded1782cf8d6");
  // A divisor whose reciprocal a slightly perturbed table gets wrong.
  const auto c2 = parse<uint128>("0x6116606c71f56a52b2b4ad691bb1474a");
  const uint128 c2d = 0x807ee4fab105d83e;
  CHECK_HEX(c2 / c2d, "c16cff885e1ea21d");
  CHECK_HEX(c2 % c2d, "1f6491f73fec8c44");
  CHECK_HEX(b / a, "0");
  CHECK_HEX(b % a, "1ffffffffffffffff");
  const auto sd = intx::sdivrem(-uint128{100}, uint128{7});
  CHECK_HEX(sd.quot, "fffffffffffffffffffffffffffffff2");
  CHECK_HEX(sd.rem, "fffffffffffffffffffffffffffffffe");

  CHECK_HEX(a << 4, "edcba98765432100123456789abcdef0");
  CHECK_HEX(a << 64, "123456789abcdef0000000000000000");
  CHECK_HEX(a << 100, "9abcdef0000000000000000000000000");
  CHECK_HEX(a >> 68, "fedcba987654321");
  CHECK_HEX(a >> 127, "1");
  CHECK_HEX(a << 128, "0");

  // The high word decides even when the low word is larger.
  const uint128 lo_big{ones, 1};
  const uint128 hi_big{0, 2};
  CHECK(lo_big < hi_big && !(hi_big < lo_big) && (lo_big <=> hi_big) < 0);
  CHECK(a > b && a >= a && a == a && a != b);

  CHECK(intx::clz(b) == 63);
  CHECK(intx::clz(b >> 64) == 127);
  CHECK_HEX(intx::bswap(a), "efcdab89674523011032547698badcfe");
  CHECK(intx::to_string(a) == "338770000845734292516042252062085074415");
  CHECK(intx::to_string(a, 36) == "f36n7gvc4enwij69yoivghx4f");
  CHECK(parse<uint128>("338770000845734292516042252062085074415") == a);
}

void test256() {
  const auto x = parse<uint256>(
      "55066263022277343669578718895168534326250603453777594175500187360389116"
      "729240");
  const auto y = parse<uint256>(
      "0x483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8");
  const auto p = parse<uint256>(
      "0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
  CHECK(x == GX && y == GY && p == P);

  CHECK_HEX(x + y,
            "c1f940f620808011b3455e91dc9813afffb3b123d4537cf2f63a51eb1208ec50");
  CHECK_HEX(x - y,
            "31838c07d338f746f7fb6699c076025e058448928748d4bfbdaab0cb1be742e0");
  CHECK_HEX(y - x,
            "ce7c73f82cc708b9080499663f89fda1fa7bb76d78b72b4042554f34e418bd20");
  CHECK_HEX(x * y,
            "29520a21508989b06ed1194129efb1517cee385a708abe44718bc509775ad540");
  CHECK(intx::addc(~uint256{}, uint256{1}).carry);
  CHECK(intx::subc(y, x).carry && !intx::subc(x, y).carry);

  // addmod's fast path (the modulus is at least 2^192, the operands may
  // exceed it slightly) and its general path, and mulmod.
  CHECK_HEX(intx::mulmod(x, y, p),
            "fd3dc529c6eb60fb9d166034cf3c1a5a72324aa9dfd3428a56d7e1ce0179fd9b");
  CHECK_HEX(intx::addmod(x, y, p),
            "c1f940f620808011b3455e91dc9813afffb3b123d4537cf2f63a51eb1208ec50");
  CHECK_HEX(intx::addmod(p + 5, p + 7, p), "c");
  CHECK_HEX(intx::addmod(~uint256{}, p - 1, p), "1000003cf");
  CHECK_HEX(intx::addmod(p - 1, p - 1, p),
            "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2d");
  const uint256 m = 1000000007;
  CHECK_HEX(intx::addmod(x, y, m), "4a08763");
  CHECK_HEX(intx::mulmod(x, y, m), "11d6d2e1");

  // Division by one, two, three and four words.
  CHECK_HEX(x / 17,
            "7295152c36756556e72d89f66806a0f789fc3946c1b2f945095cb5fb60e97f9");
  CHECK_HEX(x % 17, "f");
  const uint256 n1 = 0xfffffffffffffff1;
  CHECK_HEX(x / n1, "79be667ef9dcbbb377c8640672760a8b0759d93be2b8c6fe");
  CHECK_HEX(x % n1, "c8363bdd5fcbc07a");
  const auto n2 = (uint256{1} << 64) + 3;
  CHECK_HEX(x / n2, "79be667ef9dcbbaae8652f18e0f0d806496c6f908afba0c6");
  CHECK_HEX(x % n2, "7dad32a976053546");
  const auto n3 = (uint256{1} << 130) + 5;
  CHECK_HEX(x / n3, "1e6f999fbe772eeb156818a573a1c2c1");
  CHECK_HEX(x % n3, "26a6dfcbc757a3e41eeea061fd4cf49d3");
  CHECK_HEX(x / y, "1");
  CHECK_HEX(x % y,
            "31838c07d338f746f7fb6699c076025e058448928748d4bfbdaab0cb1be742e0");
  // One quotient digit needs the add-back step, the next overflows the 3-by-2
  // estimate.
  const auto u = parse<uint256>(
      "0xffffffffffffffff80000000000000017fffffffffffffffffffffffffffffff");
  const auto v =
      parse<uint256>("0xffffffffffffffff80000000000000018000000000000001");
  CHECK_HEX(u / v, "ffffffffffffffff");
  CHECK_HEX(u % v, "ffffffffffffffff80000000000000008000000000000000");
  // Reaches udivrem_3by2's second correction.
  const auto c3 =
      parse<uint256>("0x3bb08f3e111b4965e47da9ada74cd22f49aad652c6f6b801");
  const auto c3d = parse<uint256>("0x8000000000000000c1fc790ec11825f0");
  CHECK_HEX(c3 / c3d, "77611e7c223692cb");
  CHECK_HEX(c3 % c3d, "a07b5a4468c2e0eae95fd1e028dc2b1");
  const auto sd = intx::sdivrem(-x, uint256{3});
  CHECK_HEX(sd.quot,
            "d76b332b020bc17138ca89ce107da6fda9cc010c46109d0ce2047f8c4dad4d78");
  CHECK_HEX(sd.rem, "0");

  CHECK_HEX(intx::exp(uint256{3}, uint256{255}),
            "428f4f92af1aaa80aa46162b1f71e981273601f4ad1dd4709b5aca650265a6ab");
  CHECK_HEX(intx::exp(uint256{2}, uint256{255}),
            "8000000000000000000000000000000000000000000000000000000000000000");
  CHECK_HEX(x << 77,
            "c52b9d0e160e0537f9b65b9c51b2b3e502b62df02f300000000000000000000");
  CHECK_HEX(x >> 129, "3cdf333f7cee5dd62ad0314ae7438583");
  CHECK(intx::clz(uint256{1} << 200) == 55);
  CHECK(intx::to_string(p) == "1157920892373161954235709850086879078532699846"
                              "65640564039457584007908834671663");

  // Big-endian bytes, the order of Ethereum's 256-bit words.
  std::uint8_t bytes[32];
  intx::be::store(bytes, x);
  CHECK(bytes[0] == 0x79 && bytes[1] == 0xbe && bytes[31] == 0x98);
  CHECK(intx::be::load<uint256>(bytes) == x);
  intx::le::store(bytes, x);
  CHECK(bytes[0] == 0x98 && bytes[31] == 0x79);
  CHECK(intx::le::load<uint256>(bytes) == x);
}

void test512() {
  const auto x = parse<uint256>(
      "0x79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
  const auto y = parse<uint256>(
      "0x483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8");

  // 256 x 256 -> 512, then 512 / 256.
  const uint512 z = intx::umul(x, y);
  CHECK_HEX(z, "225989dbbc349b6f319ca3eed777a46f55b1dc22e97af11261167d213c1f060d"
               "29520a21508989b06ed1194129efb1517cee385a708abe44718bc509775ad540");
  const auto qr = intx::udivrem(z, P);
  CHECK_HEX(qr.quot,
            "225989dbbc349b6f319ca3eed777a46f55b1dc22e97af11261167d215e78906b");
  CHECK_HEX(qr.rem,
            "fd3dc529c6eb60fb9d166034cf3c1a5a72324aa9dfd3428a56d7e1ce0179fd9b");

  // 512 / 256 through the quotient-digit overflow and the add-back steps.
  const auto ov = parse<uint512>(
      "0xffffffffffffffff800000000000000100000000000000000000000000000000"
      "fffffffffffffffe000000000000000080000000000000017fffffffffffffff");
  const auto ovd = parse<uint256>(
      "0xffffffffffffffff8000000000000001fffffffffffffffe0000000000000000");
  const auto ovr = intx::udivrem(ov, ovd);
  CHECK_HEX(ovr.quot,
            "ffffffffffffffffffffffffffffffff00000000000000018000000000000003");
  CHECK_HEX(ovr.rem,
            "bffffffffffffffa7ffffffffffffffd80000000000000077fffffffffffffff");
  const auto ab = parse<uint512>(
      "0x80000000000000017ffffffffffffffffffffffffffffffe8000000000000000"
      "7fffffffffffffff7fffffffffffffff80000000000000000000000000000000");
  const auto abd = parse<uint256>(
      "0x80000000000000007fffffffffffffffffffffffffffffffffffffffffffffff");
  const auto abr = intx::udivrem(ab, abd);
  CHECK_HEX(abr.quot,
            "10000000000000001fffffffffffffffdffffffffffffffff0000000000000003");
  CHECK_HEX(abr.rem,
            "7ffffffffffffffffffffffffffffffd7fffffffffffffff0000000000000003");

  CHECK_HEX(ov << 100,
            "100000000000000000000000000000000fffffffffffffffe0000000000000000"
            "80000000000000017fffffffffffffff0000000000000000000000000");
  CHECK_HEX(ov >> 100,
            "ffffffffffffffff800000000000000100000000000000000000000000000000"
            "fffffffffffffffe00000000000000008000000");

  const auto max = ~uint512{};
  CHECK_HEX(max * max, "1");
  const auto w = max - (uint512{1} << 300) + 12346;
  CHECK_HEX(w / z, "7");
  CHECK_HEX(w % z,
            "f8d3afdda8fbff5a4b784781bba80f4a822fb0b9da3687f586284175b26d5a3d"
            "ec1b916cc3d3c2cf8484f37da7226c5957c7586ec34cc20e52d9cbdbc845b79");
  CHECK_HEX(intx::exp(uint512{3}, uint512{300}),
            "b39cfff485a5dbf4d6aae030b91bfb0ec6bba389cd8d7f85bba3985c19c5e24e"
            "40c543a123c6e028a873e9e3874e1b4623a44be39b34e67dc5c2671");
  CHECK(intx::to_string(uint512{1} << 511) ==
        "67039039649712985497870124991029230637396829102961966888617807218608"
        "82015036773488400937149083451713845015929093243025426876941405973284"
        "973216824503042048");
}

void test_errors() {
  CHECK((throws<std::invalid_argument, uint128>("123a")));
  CHECK((throws<std::invalid_argument, uint128>("0xcdefg")));
  CHECK((throws<std::out_of_range, uint128>(
      "340282366920938463463374607431768211456")));
  CHECK((throws<std::out_of_range, uint128>(
      "0x100000000000000000000000000000000")));
  bool bad_base = false;
  try {
    (void)intx::to_string(uint256{1}, 37);
  } catch (const std::invalid_argument &) {
    bad_base = true;
  }
  CHECK(bad_base);
}

} // namespace

int main() {
  test128();
  test256();
  test512();
  test_errors();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("intx: ok");
  return 0;
}
