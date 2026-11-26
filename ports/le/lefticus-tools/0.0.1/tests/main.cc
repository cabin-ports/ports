// forwarding_ref.hpp and moving_ref.hpp need C++26 deleted-function reasons.
#if !defined(_MSC_VER) && (defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 15))
#define HAS_DELETE_REASON 1
#else
#define HAS_DELETE_REASON 0
#endif

#include <lefticus/tools/curry.hpp>
#if HAS_DELETE_REASON
#include <lefticus/tools/forwarding_ref.hpp>
#include <lefticus/tools/moving_ref.hpp>
#endif
#include <lefticus/tools/simple_stack_flat_map.hpp>
#include <lefticus/tools/simple_stack_vector.hpp>
#include <lefticus/tools/type_lists.hpp>

#include <cstdio>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tools = lefticus::tools;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

constexpr auto sum3 = [](int x, int y, int z) { return x + y + z; };
static_assert(tools::curry(sum3, 1, 2, 3) == 6);
static_assert(tools::curry(sum3, 1)(2, 3) == 6);

using list = tools::type_list<float, double, int>;
static_assert(std::is_same_v<tools::nth_t<2, list>, int>);
static_assert(std::is_same_v<tools::nth_t<0, list>, float>);
static_assert(std::is_same_v<tools::split_n_t<1, list>,
                             tools::type_pair<tools::type_list<float>, tools::type_list<double, int>>>);
static_assert(std::is_same_v<tools::join_t<tools::type_list<float>, tools::type_list<double, int>>, list>);
static_assert(std::is_same_v<tools::sub_t<1, 1, list>, tools::type_list<double>>);

#if HAS_DELETE_REASON
struct CountsMovesCopies {
  CountsMovesCopies() = default;
  CountsMovesCopies(CountsMovesCopies &&other) noexcept { ++other.moved_from; }
  CountsMovesCopies(const CountsMovesCopies &other) noexcept { ++other.copied_from; }
  CountsMovesCopies &operator=(CountsMovesCopies &&other) noexcept {
    ++other.move_assigned_from;
    return *this;
  }
  CountsMovesCopies &operator=(const CountsMovesCopies &other) noexcept {
    ++other.copy_assigned_from;
    return *this;
  }

  mutable int moved_from = 0;
  mutable int copied_from = 0;
  mutable int move_assigned_from = 0;
  mutable int copy_assigned_from = 0;
};

struct CopyOnly {
  CopyOnly() = default;
  CopyOnly(const CopyOnly &) = default;
  CopyOnly &operator=(const CopyOnly &) = default;
};

template <typename T> constexpr bool can_make_moving_ref() {
  return requires(T t) { tools::moving_ref<T>{std::move(t)}; };
}
template <typename T> constexpr bool can_make_moving_ref_from_const() {
  return requires(const T t) { tools::moving_ref<T>{std::move(t)}; };
}
static_assert(can_make_moving_ref<CountsMovesCopies>());
static_assert(!can_make_moving_ref_from_const<CountsMovesCopies>());
static_assert(!can_make_moving_ref<CopyOnly>());
#endif
}  // namespace

int main() {
  // curry
  check(tools::curry(sum3)(1, 2, 3) == 6, "curry no args");
  check(tools::curry(sum3, 10)(20)(30) == 60, "curry one at a time");
  check(tools::curry(sum3, 1, 2)(4) == 7, "curry two then one");

#if HAS_DELETE_REASON
  // forwarding_ref: converting back to a value copies from an lvalue and moves from an rvalue
  {
    CountsMovesCopies lvalue;
    tools::forwarding_ref ref{lvalue};
    [[maybe_unused]] CountsMovesCopies copy = ref;
    check(lvalue.copied_from == 1 && lvalue.moved_from == 0, "forwarding_ref lvalue copies");

    CountsMovesCopies rvalue;
    tools::forwarding_ref mref{std::move(rvalue)};
    [[maybe_unused]] CountsMovesCopies moved = mref;
    check(rvalue.moved_from == 1 && rvalue.copied_from == 0, "forwarding_ref rvalue moves");
  }

  // moving_ref: construction and assignment through it move
  {
    CountsMovesCopies source;
    tools::moving_ref ref{std::move(source)};
    [[maybe_unused]] CountsMovesCopies constructed = ref;
    check(source.moved_from == 1 && source.copied_from == 0, "moving_ref move construct");

    CountsMovesCopies source2;
    tools::moving_ref ref2{std::move(source2)};
    CountsMovesCopies target;
    target = ref2;
    check(source2.move_assigned_from == 1 && source2.copy_assigned_from == 0, "moving_ref move assign");
  }
#endif

  // simple_stack_vector
  tools::simple_stack_vector<int, 3> vec{1, 2};
  check(vec.size() == 2 && vec.max_size() == 3, "vector size");
  vec.push_back(3);
  check(vec.size() == 3 && vec[2] == 3 && vec.at(0) == 1, "vector push_back");
  bool threw = false;
  try {
    vec.push_back(4);
  } catch (const std::length_error &) {
    threw = true;
  }
  check(threw, "vector overflow throws");
  check(vec == (tools::simple_stack_vector<int, 3>{1, 2, 3}), "vector equality");

  // simple_stack_flat_map
  tools::simple_stack_flat_map<int, int, 5> map;
  check(map.empty() && map.max_size() == 5, "map starts empty");
  map[3] = 30;
  map[1] = 10;
  map[3] = 33;
  check(map.size() == 2 && map.at(1) == 10 && map.at(3) == 33, "map insert and overwrite");
  check(map.begin()->first == 3 && (map.begin() + 1)->first == 1, "map keeps insertion order");
  threw = false;
  try {
    (void)map.at(2);
  } catch (const std::out_of_range &) {
    threw = true;
  }
  check(threw, "map at missing key throws");

  return failures == 0 ? 0 : 1;
}
