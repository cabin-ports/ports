// Values from upstream's src/test/tuple/tuple_main.cpp:13-53 and tuple_cat.cpp:13-29.
#include <tao/tuple/tuple.hpp>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <type_traits>

#define CHECK(...)                                  \
  do {                                              \
    if (!(__VA_ARGS__)) {                           \
      std::printf("FAIL: %s\n", #__VA_ARGS__);      \
      return 1;                                     \
    }                                               \
  } while (0)

int main() {
  using namespace tao;

  auto t = tuple<int, double, int>();
  auto t2 = tuple<int, double, int>(1, 2, 3);
  auto t3(t2);
  auto t4 = tuple<int, double, int>(1, 2, 2);
  auto t5 = tuple<int, double, int>(1, 2, 4);

  static_assert(tuple_size<decltype(t)>::value == 3, "size");
  static_assert(tuple_size<decltype(t2)>::value == 3, "size");

  // default construction value-initializes
  CHECK(get<0>(t) == 0 && get<1>(t) == 0.0);

  // get by index and by type
  CHECK(get<0>(t2) == 1);
  CHECK(get<1>(t2) == 2);
  CHECK(get<2>(t2) == 3);
  CHECK(get<double>(t2) == 2);
  CHECK(get<2>(t3) == 3);

  // comparisons
  CHECK(t2 == t3);
  CHECK(t2 != t4);
  CHECK(t2 > t4);
  CHECK(t2 < t5);
  CHECK(t2 >= t3);
  CHECK(t2 <= t3);
  CHECK(tuple<>() == tuple<>());
  CHECK(!(tuple<>() < tuple<>()));
  CHECK(make_tuple(1, 2, 3) > make_tuple(1, 2, 2));
  CHECK(make_tuple(1, 2, 3) < make_tuple(1, 2, 4));
  CHECK((tuple<int, int, int>(std::allocator_arg, std::allocator<int>(), 1, 2, 3) ==
         make_tuple(1, 2, 3)));

  // tuple_cat
  auto c = tuple_cat(make_tuple(1, true), make_tuple(2.0), make_tuple(),
                     make_tuple(3UL, nullptr));
  static_assert(std::is_same<decltype(c), tuple<int, bool, double, unsigned long,
                                                std::nullptr_t>>::value, "cat");
  CHECK(get<0>(c) == 1);
  CHECK(get<bool>(c) == true);
  CHECK(get<2>(c) == 2.0);
  CHECK(get<unsigned long>(c) == 3UL);
  CHECK(get<std::nullptr_t>(c) == nullptr);

  // reference elements (std::reference_wrapper unwrapping, tuple.hpp:612)
  int x = 7;
  auto r = tao::make_tuple(std::ref(x));
  static_assert(std::is_same<decltype(r), tuple<int&>>::value, "ref");
  get<0>(r) = 9;
  CHECK(x == 9);

  std::printf("ok\n");
  return 0;
}
