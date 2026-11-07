// Consumer test for cabin-ports/functionalplus: functional-style container
// algorithms, Maybe/Result, forward application, currying, and the
// thread-backed *_parallelly functions.
#include <fplus/fplus.hpp>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <list>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

bool is_odd(int x) { return x % 2 != 0; }
int times_3(int x) { return 3 * x; }
int square(int x) { return x * x; }
int digits(int x) { return static_cast<int>(std::to_string(x).size()); }

std::list<int> collatz_seq(int x) {
  std::list<int> seq{x};
  while (x > 1) {
    x = x % 2 == 0 ? x / 2 : 3 * x + 1;
    seq.push_back(x);
  }
  return seq;
}

struct cat {
  double cuteness() const {
    return softness * temperature * roundness * fur_amount - size;
  }
  std::string name;
  double softness, temperature, size, roundness, fur_amount;
};

// The README's examples (README.md:66-68, :79-195).
void test_readme() {
  const std::vector<int> values{24, 11, 65, 44, 80, 18, 73, 90, 69, 18};
  check(fplus::keep_if(is_odd, values) == std::vector<int>{11, 65, 73, 69},
        "keep_if keeps the odd values");

  check(fplus::all_the_same(std::list<std::string>{"same old", "same old"}),
        "all_the_same accepts equal elements");
  check(!fplus::all_the_same(std::vector<int>{1, 1, 2}),
        "all_the_same rejects a different element");

  const std::string team = "Our team is great. I love everybody I work with.";
  check(fplus::count(std::string("I"), fplus::split_words(false, team)) == 2,
        "count finds two words \"I\"");

  const std::vector<cat> cats{{"Tigger", 5, 5, 5, 5, 5},
                              {"Simba", 2, 9, 9, 2, 7},
                              {"Muffin", 9, 4, 2, 8, 6},
                              {"Garfield", 6, 5, 7, 9, 5}};
  check(fplus::maximum_on(std::mem_fn(&cat::cuteness), cats).name == "Muffin",
        "maximum_on picks the element with the largest key");

  using Ints = std::list<int>;
  const auto xs = fplus::numbers<int, Ints>(1, 30);
  check(xs.size() == 29 && xs.front() == 1 && xs.back() == 29,
        "numbers is half-open");
  const auto show_ints =
      fplus::bind_1st_of_2(fplus::show_cont_with<Ints>, " => ");
  const auto show_collatz_seq = fplus::compose(collatz_seq, show_ints);
  auto collatz_dict = fplus::create_map_with(show_collatz_seq, xs);
  check(collatz_dict.size() == 29, "create_map_with maps every key");
  check(collatz_dict[13] == "[13 => 40 => 20 => 10 => 5 => 16 => 8 => 4 => 2 => 1]",
        "compose and bind_1st_of_2 build the shown sequence");
}

void test_containers() {
  check(fplus::split(0, true, std::vector<int>{1, 3, 2, 0, 0, 6, 0, 7, 5}) ==
            std::vector<std::vector<int>>{{1, 3, 2}, {}, {6}, {7, 5}},
        "split keeps empty groups when asked");
  check(fplus::split(0, false, std::vector<int>{0, 1, 0, 0, 2}) ==
            std::vector<std::vector<int>>{{1}, {2}},
        "split drops empty groups otherwise");
  check(fplus::join(std::string(", "),
                    std::vector<std::string>{"a", "bee", "cee"}) ==
            "a, bee, cee",
        "join inserts the separator");
  check(fplus::show_cont(std::vector<int>{1, 2, 3}) == "[1, 2, 3]",
        "show_cont formats a sequence");
  check(fplus::sort(std::vector<int>{3, 1, 2}) == std::vector<int>{1, 2, 3},
        "sort orders ascending");
  check(fplus::unique(std::vector<int>{1, 2, 2, 3, 2}) ==
            std::vector<int>{1, 2, 3, 2},
        "unique collapses consecutive duplicates only");
  check(fplus::nub(std::vector<int>{1, 2, 2, 3, 2}) == std::vector<int>{1, 2, 3},
        "nub removes every duplicate");
  check(fplus::count_occurrences(std::vector<int>{1, 2, 2, 3, 2}) ==
            std::map<int, std::size_t>{{1, 1}, {2, 3}, {3, 1}},
        "count_occurrences builds a histogram");
  check(fplus::trim_whitespace(std::string("  hi there \t")) == "hi there",
        "trim_whitespace strips both ends");
  check(fplus::split_lines(true, std::string("one\r\ntwo\n\nthree")) ==
            std::vector<std::string>{"one", "two", "", "three"},
        "split_lines counts CRLF as one break and keeps empty lines");
}

void test_maybe_result() {
  check(fplus::read_value<int>("42") == fplus::just(42),
        "read_value parses an int");
  check(fplus::is_nothing(fplus::read_value<int>("4x")),
        "read_value rejects trailing garbage");
  check(fplus::read_value_result<int>("x").is_error(),
        "read_value_result reports the error");
  check(fplus::lift_maybe(square, fplus::just(3)) == fplus::just(9),
        "lift_maybe maps a just");
  check(fplus::just_with_default(7, fplus::nothing<int>()) == 7,
        "just_with_default falls back on nothing");

  const auto safe_div = [](int d) {
    return [d](int n) {
      return d == 0 ? fplus::error<int, std::string>("division by zero")
                    : fplus::ok<int, std::string>(n / d);
    };
  };
  const auto ok = fplus::and_then_result(safe_div(4),
                                         fplus::ok<int, std::string>(20));
  check(ok.is_ok() && ok.unsafe_get_ok() == 5, "and_then_result chains an ok");
  const auto err = fplus::and_then_result(safe_div(0),
                                          fplus::ok<int, std::string>(20));
  check(err.is_error() && err.unsafe_get_error() == "division by zero",
        "and_then_result passes the error on");
  check(fplus::ok_with_default(-1, err) == -1,
        "ok_with_default falls back on an error");
}

// fwd:: and curry:: come from the shipped *.autogenerated_defines files.
void test_fwd_curry() {
  const int old_style = fplus::sum(fplus::transform(
      digits, fplus::drop_if(is_odd, fplus::transform(times_3,
                                                      fplus::numbers(0, 10)))));
  const int new_style = fplus::fwd::apply(
      fplus::numbers(0, 10), fplus::fwd::transform(times_3),
      fplus::fwd::drop_if(is_odd), fplus::fwd::transform(digits),
      fplus::fwd::sum());
  check(old_style == 8 && new_style == 8,
        "fwd::apply matches the nested call (0, 6, 12, 18, 24 -> 8 digits)");

  const auto chain = fplus::fwd::compose(fplus::fwd::transform(times_3),
                                         fplus::fwd::keep_if(is_odd),
                                         fplus::fwd::sum());
  check(chain(std::vector<int>{1, 2, 3}) == 12,
        "fwd::compose builds a pipeline");

  check(fplus::curry::show_fill_left(' ')(std::size_t(4))(3) == "   3",
        "curry applies one argument at a time");
}

void test_parallel() {
  const auto main_id = std::this_thread::get_id();
  const auto ids = fplus::transform_parallelly(
      [](int) { return std::this_thread::get_id(); },
      std::vector<int>{1, 2, 3, 4});
  check(ids.size() == 4 && !fplus::is_elem_of(main_id, ids),
        "transform_parallelly runs off the calling thread");

  const auto xs = fplus::numbers(1, 101);
  check(fplus::transform_parallelly(square, std::vector<int>{1, 3, 4}) ==
            std::vector<int>{1, 9, 16},
        "transform_parallelly keeps the input order");
  check(fplus::transform_parallelly_n_threads(3, times_3, xs) ==
            fplus::transform(times_3, xs),
        "transform_parallelly_n_threads matches transform");
  check(fplus::reduce_parallelly(std::plus<int>(), 0, xs) == 5050,
        "reduce_parallelly sums 1..100");
  check(fplus::transform_reduce_parallelly(square, std::plus<int>(), 0,
                                           std::vector<int>{1, 2, 3}) == 14,
        "transform_reduce_parallelly sums squares");
  check(fplus::keep_if_parallelly(is_odd, fplus::numbers(0, 10)) ==
            std::vector<int>{1, 3, 5, 7, 9},
        "keep_if_parallelly filters in order");
}

}  // namespace

int main() {
  test_readme();
  test_containers();
  test_maybe_result();
  test_fwd_curry();
  test_parallel();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("functionalplus consumer ok");
  return 0;
}
