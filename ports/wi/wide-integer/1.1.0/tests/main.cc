#include <math/wide_integer/uintwide_t.h>

#include <cstdint>
#include <cstdio>

using ::math::wide_integer::int128_t;
using ::math::wide_integer::uint256_t;
using ::math::wide_integer::uint512_t;

static int failures = 0;

#define CHECK(cond)                                                                  \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                                    \
    }                                                                                \
  } while (0)

int main() {
  // 3^301: README.md:124 (the decimal value in its comment).
  const uint512_t p3 = pow(uint512_t{3U}, 301);
  CHECK(p3 == uint512_t("410674437175765127973978082146264947899391086876012309414440570235106991532497229781400618467066824164751453321793982128440538198297087323698003"));

  // examples/example001_mul_div.cpp:35-40: 256x256 -> 512-bit product, quotient 10.
  const uint256_t a("0xF4DF741DE58BCB2F37F18372026EF9CBCFC456CB80AF54D53BDEED78410065DE");
  const uint256_t b("0x166D63E0202B3D90ECCEAA046341AB504658F55B974A7FD63733ECF89DD0DF75");
  CHECK(uint512_t(a) * uint512_t(b) == uint512_t("0x1573D6A7CEA734D99865C4F428184983CDB018B80E9CC44B83C773FBE11993E7E491A360C57EB4306C61F9A04F7F7D99BE3676AAD2D71C5592D5AE70F84AF076"));
  CHECK(a / b == uint256_t(10U));

  // examples/example001a_div_mod.cpp:25-34: quotient and remainder.
  const uint512_t n("698937339790347543053797400564366118744312537138445607919548628175822115805812983955794321304304417541511379093392776018867245622409026835324102460829431");
  const uint512_t d("100041341335406267530943777943625254875702684549707174207105689918734693139781");
  CHECK(n / d == uint512_t("6986485091668619828842978360442127600954041171641881730123945989288792389271"));
  CHECK(n % d == uint512_t("100041341335406267530943777943625254875702684549707174207105689918734693139780"));

  // examples/example006_gcd.cpp:26-31.
  const uint256_t g1("0x1035452A5197CF882B5B5EB64C8CCFEE4D772F9F7B66A239649A43093464EFF5");
  const uint256_t g2("0xB4F6151D727361113083D9A0DEB91B0B62A250F65DA6543823703D0140C873AD");
  CHECK(gcd(g1, g2) == uint256_t(11759761U));

  // examples/example005_powm.cpp:23-30: modular exponentiation.
  const uint256_t pb("0xDA4033C9B1B0675C20B7879EA63FFFBEEBEC3F89F78D22C393FAD98E7AE9BF69");
  const uint256_t pe("0xA4748AD2DAFEED29C73927BD0945EF45EFEC9DAA95CC59390D406FC27236A174");
  const uint256_t pm("0xB6EC4DAB21E2856D488D669C210DC1FAD00366F92D602B1D42B88E24531F907E");
  CHECK(powm(pb, pe, pm) == uint256_t("0x5231F0EF6BBB3E78B9D7B1FA5F86EFA932E71BABD8A1CFF2C9EE5C396284ED07"));

  // Signed int128_t behaves like built-in int (README.md:70): truncating division.
  const int128_t m{-35};
  const int128_t q{4};
  CHECK(m * int128_t{7} == int128_t{-245});
  CHECK(m / q == int128_t{-8});
  CHECK(m % q == int128_t{-3});
  CHECK(m < int128_t{0});

  return failures == 0 ? 0 : 1;
}
