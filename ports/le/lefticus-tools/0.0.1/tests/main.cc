#include <lefticus/tools/curry.hpp>
#include <lefticus/tools/simple_stack_flat_map.hpp>
#include <lefticus/tools/simple_stack_vector.hpp>
#include <lefticus/tools/type_lists.hpp>

#include <cstdio>
#include <stdexcept>
#include <type_traits>

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

}  // namespace

int main() {
  // curry
  check(tools::curry(sum3)(1, 2, 3) == 6, "curry no args");
  check(tools::curry(sum3, 10)(20)(30) == 60, "curry one at a time");
  check(tools::curry(sum3, 1, 2)(4) == 7, "curry two then one");

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
