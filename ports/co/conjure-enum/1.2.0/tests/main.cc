#include <fix8/conjure_enum.hpp>
#include <fix8/conjure_enum_bitset.hpp>

#include <cstdio>
#include <string_view>

using namespace FIX8;

enum class color : int { red, green, blue, yellow };

static int failures;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  using ce = conjure_enum<color>;

  CHECK(ce::enum_to_string(color::green) == "color::green");
  CHECK(ce::enum_to_string(color::yellow, true) == "yellow");
  CHECK(ce::enum_to_string(static_cast<color>(9)).empty());
  CHECK(ce::enum_to_string<color::blue>() == "color::blue");

  CHECK(ce::string_to_enum("color::blue") == color::blue);
  CHECK(!ce::string_to_enum("color::purple").has_value());

  CHECK(ce::count() == 4);
  CHECK(ce::contains(color::red));
  CHECK(!ce::contains(static_cast<color>(4)));
  CHECK(ce::contains("color::yellow"));
  CHECK(!ce::contains("yellow"));

  int sum = 0, calls = 0;
  ce::for_each([&](color c) { sum += static_cast<int>(c); ++calls; });
  CHECK(calls == 4 && sum == 0 + 1 + 2 + 3);

  enum_bitset<color> bits(color::red, color::blue);
  CHECK(bits.count() == 2);
  CHECK(bits.test(color::red) && !bits.test(color::green) && bits.test(color::blue));
  CHECK(bits.to_string() == "0101");
  bits |= color::yellow;
  CHECK(bits.to_ulong() == 0b1101);
  CHECK(!bits.all() && bits.any());

  return failures != 0;
}
