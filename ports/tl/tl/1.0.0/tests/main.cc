// tl utilities, checked with exact results.  typelist.hpp comes first so it
// is the only header seen before its own includes.
#include <tl/typelist.hpp>

#include <tl/apply.hpp>
#include <tl/casts.hpp>
#include <tl/integer_sequence.hpp>
#include <tl/make_array.hpp>
#include <tl/numeric_aliases.hpp>
#include <tl/overload.hpp>

#include <cstdio>
#include <tuple>
#include <type_traits>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

static_assert(tl::make_index_sequence<4>::size() == 4, "make_index_sequence");
static_assert(std::is_same<tl::index_typelist<tl::typelist<int, char, double>, 2>::type,
                           double>::value,
              "index_typelist");
static_assert(std::is_same<tl::cat_typelist<tl::typelist<int>, tl::typelist<char>>::type,
                           tl::typelist<int, char>>::value,
              "cat_typelist");
static_assert(std::is_same<tl::u8, unsigned char>::value && sizeof(tl::u8) == 1,
              "u8");

enum class Color : short { Red = 3 };

}  // namespace

int main() {
  auto a = tl::make_array(1, 2, 3);
  CHECK(a.size() == 3);
  CHECK(a[0] == 1 && a[2] == 3);

  auto o = tl::overload([](int x) { return x + 1; }, [](double) { return 2; });
  CHECK(o(1) == 2);
  CHECK(o(41) == 42);

  CHECK(tl::apply([](int x, int y) { return x + y; }, std::make_tuple(2, 3)) == 5);

  CHECK(tl::bit_cast<tl::u32>(1.0f) == 0x3f800000u);
  CHECK(tl::underlying_cast(Color::Red) == 3);

  return failures == 0 ? 0 : 1;
}
