// Values from upstream's src/test/seq/*.cpp. Each CHECK is a static_assert
// mirrored by a run-time check of the same expression.
#include <cstdio>
#include <type_traits>

#include <tao/seq/at_index.hpp>
#include <tao/seq/integer_sequence.hpp>
#include <tao/seq/make_integer_range.hpp>
#include <tao/seq/make_integer_sequence.hpp>
#include <tao/seq/max.hpp>
#include <tao/seq/partial_sum.hpp>
#include <tao/seq/reverse.hpp>
#include <tao/seq/select.hpp>
#include <tao/seq/sum.hpp>

#define CHECK(...)                       \
  do {                                   \
    static_assert((__VA_ARGS__), #__VA_ARGS__); \
    if (!(__VA_ARGS__)) {                \
      std::printf("FAIL: %s\n", #__VA_ARGS__); \
      return 1;                          \
    }                                    \
  } while (0)

int main() {
  using namespace tao::sequence;

  // sum.cpp:9-27
  CHECK(sum<int>::value == 0);
  CHECK((sum<int, 0, 1, -1, 0, -1, 0, 1>::value == 0));
  CHECK((sum<unsigned, 3, 1, 5, 0, 1>::value == 10));
  CHECK((sum<index_sequence<4, 9, 1, 123, 2>>::value == 139));
  CHECK((sum<make_index_sequence<10>>::value == 45));

  // at_index.cpp:15,25-26
  CHECK((std::is_same<at_index_t<0, int>, int>::value));
  CHECK((std::is_same<at_index_t<1, void, int>, int>::value));
  CHECK((!std::is_same<at_index_t<0, void, int>, int>::value));

  // reverse.cpp:13-16
  CHECK((std::is_same<reverse_t<int, 1, 2, 3, 4, 5>,
                      integer_sequence<int, 5, 4, 3, 2, 1>>::value));
  CHECK((std::is_same<reverse_t<index_sequence<1, 4, 3, 7, 5>>,
                      index_sequence<5, 7, 3, 4, 1>>::value));

  // make_integer_range.cpp:19-28
  CHECK((std::is_same<make_integer_range<int, 1, 5>,
                      integer_sequence<int, 1, 2, 3, 4>>::value));
  CHECK((std::is_same<make_integer_range<int, 3, -3>,
                      integer_sequence<int, 3, 2, 1, 0, -1, -2>>::value));
  CHECK((std::is_same<make_index_range<42, 42>, index_sequence<>>::value));

  // select.cpp:12-24
  using S = integer_sequence<int, 4, 7, -2, 0, 3>;
  CHECK((tao::sequence::select<1, S>::value == 7));
  CHECK((tao::sequence::select<2, S>::value == -2));
  CHECK((std::is_same<tao::sequence::select<0, integer_sequence<unsigned, 42>>::value_type,
                      unsigned>::value));

  // partial_sum.cpp:26-36
  using T = integer_sequence<int, 2, 4, 6, 3, 5, 1>;
  CHECK((partial_sum<4, int, 0, 1, 2, 3>::value == 6));
  CHECK((partial_sum<3, T>::value == 12));
  CHECK((partial_sum<4, int, 0, -1, 2, -3>::value == -2));

  // max.cpp:11-18
  CHECK((max<int, -1, 2, 1>::value == 2));
  CHECK((max<int, -1, 1>::value == 1));

  std::printf("ok\n");
  return 0;
}
