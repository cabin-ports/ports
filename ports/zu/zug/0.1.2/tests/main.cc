// zug: transducer pipelines give the results upstream's own tests expect
// (test/, zug v0.1.2).  Where a skipping transducer (filter, drop_while,
// read) feeds a stateful one (take, write, or sequence's own state), the
// state is a zug::skip_state, the variant zug picks by language standard;
// the program prints which variant it was compiled with.
#include <zug/compose.hpp>
#include <zug/into.hpp>
#include <zug/into_vector.hpp>
#include <zug/run.hpp>
#include <zug/sequence.hpp>
#include <zug/skip.hpp>
#include <zug/transduce.hpp>
#include <zug/transducer/cat.hpp>
#include <zug/transducer/count.hpp>
#include <zug/transducer/dedupe.hpp>
#include <zug/transducer/distinct.hpp>
#include <zug/transducer/drop_while.hpp>
#include <zug/transducer/enumerate.hpp>
#include <zug/transducer/filter.hpp>
#include <zug/transducer/interpose.hpp>
#include <zug/transducer/map.hpp>
#include <zug/transducer/mapcat.hpp>
#include <zug/transducer/partition.hpp>
#include <zug/transducer/partition_by.hpp>
#include <zug/transducer/read.hpp>
#include <zug/transducer/scan.hpp>
#include <zug/transducer/take.hpp>
#include <zug/transducer/take_while.hpp>
#include <zug/transducer/write.hpp>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

using ints = std::vector<int>;
using int_rows = std::vector<std::vector<int>>;

}  // namespace

int main() {
  using namespace zug;
  const auto odd = [](int x) { return x % 2 != 0; };
  const auto even = [](int x) { return x % 2 == 0; };
  const auto times2 = [](int x) { return x * 2; };

  // test/transducer/filter.cpp: "filter, example" and the compositions.
  CHECK(into_vector(filter(odd), ints{1, 2, 3, 4, 5, 6}) == (ints{1, 3, 5}));
  CHECK(transduce(comp(filter(even), map(times2)), std::plus<int>{}, 1,
                  ints{1, 2, 3, 6}) == 17);
  CHECK(transduce(filter(even) | map(times2), std::plus<int>{}, 1,
                  ints{1, 2, 3, 6}) == 17);
  // "filter, compose with stateful transducer": take(2) wraps the state, so
  // filter's skipped steps go through the variant.
  CHECK(transduce(comp(filter(even), take(2)), std::plus<int>{}, 1,
                  ints{1, 2, 3, 4, 5, 6}) == 7);

  // The README's pipeline, into a vector of strings.
  {
    const auto xf = filter([](int x) { return x > 0; }) |
                    map([](int x) { return std::to_string(x); });
    CHECK(into(std::vector<std::string>{}, xf, ints{3, -2, 42, -10}) ==
          (std::vector<std::string>{"3", "42"}));
  }

  // take_while.cpp: on a sequence, and ending a nullary generator.
  CHECK(into_vector(take_while([](int x) { return x < 4; }),
                    ints{1, 2, 3, 4, 3}) == (ints{1, 2, 3}));
  {
    int n = 0;
    CHECK(transduce(comp(map([&] { return n++; }),
                         take_while([](int x) { return x < 10; })),
                    std::plus<int>{}, 1) == 46);
  }

  // drop_while.cpp
  CHECK(into_vector(drop_while([](int x) { return x < 4; }),
                    ints{1, 2, 3, 4, 3, 2, 1}) == (ints{4, 3, 2, 1}));
  CHECK(into_vector(comp(drop_while([](int x) { return x < 3; }), take(2)),
                    ints{1, 2, 3, 4, 5}) == (ints{3, 4}));

  // partition.cpp and partition_by.cpp
  CHECK(into_vector(partition(2u), ints{1, 2, 3, 4, 5, 6, 7}) ==
        (int_rows{{1, 2}, {3, 4}, {5, 6}, {7}}));
  CHECK(into(int_rows{}, comp(take(3), partition(2u)), ints{1, 2, 3, 4, 5}) ==
        (int_rows{{1, 2}, {3}}));
  CHECK(into_vector(partition_by([](int x) { return x % 2; }),
                    ints{1, 1, 2, 4, 2, 3}) == (int_rows{{1, 1}, {2, 4, 2}, {3}}));

  // dedupe.cpp, distinct.cpp, interpose.cpp, cat.cpp, mapcat.cpp, scan.cpp
  CHECK(into_vector(dedupe, ints{1, 1, 2, 1, 1, 3, 2, 2, 2, 1}) ==
        (ints{1, 2, 1, 3, 2, 1}));
  CHECK(into_vector(distinct, ints{1, 2, 1, 3, 2, 1}) == (ints{1, 2, 3}));
  CHECK(into_vector(comp(interpose(42), take(4)), ints{1, 2, 3, 4, 5}) ==
        (ints{1, 42, 2, 42}));
  CHECK(into_vector(cat, int_rows{{1, 2}, {3}, {4, 5, 6}}) ==
        (ints{1, 2, 3, 4, 5, 6}));
  CHECK(into_vector(mapcat(times2), int_rows{{1, 2}, {3}, {4, 5, 6}}) ==
        (ints{2, 4, 6, 8, 10, 12}));
  CHECK(into_vector(scan(0, std::plus<int>{}), ints{1, 2, 3, 4, 5, 6}) ==
        (ints{1, 3, 6, 10, 15, 21}));
  CHECK(into_vector(count(1) | scan(0, take(6)(std::plus<int>{}))) ==
        (ints{1, 3, 6, 10, 15, 21}));

  // enumerate.cpp
  CHECK(into_vector(enumerate, std::string("hello")) ==
        (std::vector<std::tuple<std::size_t, char>>{
            std::make_tuple(0, 'h'), std::make_tuple(1, 'e'),
            std::make_tuple(2, 'l'), std::make_tuple(3, 'l'),
            std::make_tuple(4, 'o')}));

  // read.cpp and run.cpp: a stream source that stops at bad input, and a
  // stream sink.
  {
    std::stringstream in("1 hello 2 my 3 friend 4 !!");
    CHECK(into_vector(read<int, std::string>(in)) ==
          (std::vector<std::tuple<int, std::string>>{
              std::make_tuple(1, "hello"), std::make_tuple(2, "my"),
              std::make_tuple(3, "friend"), std::make_tuple(4, "!!")}));
    std::stringstream bad("1 2 3 x 4 5 6");
    CHECK(into_vector(read<int>(bad)) == (ints{1, 2, 3}));
    std::stringstream input("1 2 3 4 5");
    std::stringstream output;
    run(comp(read<int>(input), write(output, ';')));
    CHECK(output.str() == "1;2;3;4;5");
  }

  // sequence.cpp: the lazy iterator over a filtered, mapped vector.
  {
    const ints v{1, 2, 3, 4};
    auto seq = sequence<int>(comp(filter(even), map(times2)), v);
    ints r;
    std::copy(std::begin(seq), std::end(seq), std::back_inserter(r));
    CHECK(r == (ints{4, 8}));
  }

  if (failures != 0) {
    return 1;
  }
#if ZUG_VARIANT_STD
  std::printf("zug ok (std::variant)\n");
#else
  std::printf("zug ok (boost::variant)\n");
#endif
  return 0;
}
