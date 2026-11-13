// A C++11 consumer of boolinq (a C++-only library): sources from arrays and
// standard containers, filters, transforms, ordering, grouping, aggregators,
// the end-of-sequence exception, generators, and the byte and bit views.
#include <boolinq/boolinq.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <list>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

using boolinq::from;

struct Man {
  std::string name;
  int age;
};

struct Message {
  std::string sender;
  std::string receiver;
};

void test_filters_and_transforms() {
  const std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  const std::vector<int> odd_squares = from(v).where([](int x) { return x % 2 == 1; })
                                           .select([](int x) { return x * x; })
                                           .toStdVector();
  CHECK((odd_squares == std::vector<int>{1, 9, 25, 49, 81}));
  CHECK(from(v).sum() == 55);
  CHECK(from(v).avg<double>() == 5.5);
  CHECK(from(v).min() == 1 && from(v).max() == 10);
  CHECK(from(v).max([](int x) { return -x; }) == 1);
  CHECK(from(v).count() == 10);
  CHECK(from(v).count([](int x) { return x > 7; }) == 3);
  CHECK(from(v).any([](int x) { return x > 9; }));
  CHECK(!from(v).all([](int x) { return x < 10; }));
  CHECK(from(v).contains(7) && !from(v).contains(11));

  CHECK((from(v).skip(2).take(3).toStdVector() == std::vector<int>{3, 4, 5}));
  CHECK((from(v).takeWhile([](int x) { return x < 4; }).toStdVector() ==
         std::vector<int>{1, 2, 3}));
  CHECK((from(v).skipWhile([](int x) { return x < 8; }).toStdVector() ==
         std::vector<int>{8, 9, 10}));
  CHECK((from(v).take(3).reverse().toStdVector() == std::vector<int>{3, 2, 1}));
  CHECK((from(v).where_i([](int, int i) { return i % 4 == 0; }).toStdVector() ==
         std::vector<int>{1, 5, 9}));
  CHECK(from(v).elementAt(3) == 4);
  CHECK(from(v).first([](int x) { return x > 4; }) == 5);
  CHECK(from(v).last() == 10);
  CHECK(from(v).last([](int x) { return x < 4; }) == 3);
  CHECK((from(v).take(2).append(20, 30).prepend(0).toStdVector() ==
         std::vector<int>{0, 1, 2, 20, 30}));
  const std::vector<int> tail = {11, 12};
  CHECK((from(v).concat(from(tail)).skip(8).toStdVector() ==
         std::vector<int>{9, 10, 11, 12}));
  CHECK((from(v).take(3).selectMany([](int x) { return boolinq::repeat(x, x); })
             .toStdVector() == std::vector<int>{1, 2, 2, 3, 3, 3}));
  CHECK((from(v).take(2).cast<double>().toStdVector() ==
         std::vector<double>{1.0, 2.0}));

  std::ostringstream os;
  os << from(v).take(3);
  CHECK(os.str() == "1 2 3 ");

  int weighted = 0;
  from(v).take(3).for_each_i([&weighted](int x, int i) { weighted += x * i; });
  CHECK(weighted == 1 * 0 + 2 * 1 + 3 * 2);
}

void test_structs_and_grouping() {
  Man people[] = {
      {"Kevin", 14}, {"Anton", 18}, {"Agata", 17}, {"Terra", 20}, {"Layer", 15},
  };
  const std::vector<std::string> minors =
      from(people)
          .where([](const Man& m) { return m.age < 18; })
          .orderBy([](const Man& m) { return m.age; })
          .select([](const Man& m) { return m.name; })
          .toStdVector();
  CHECK((minors == std::vector<std::string>{"Kevin", "Layer", "Agata"}));

  Message messages[] = {
      {"Anton", "Troll"}, {"Denis", "Wride"}, {"Anton", "Papay"},
      {"Denis", "Maloy"}, {"Denis", "Wride"},
  };
  CHECK(from(messages)
            .where([](const Message& m) { return m.sender == "Denis"; })
            .distinct([](const Message& m) { return m.receiver; })
            .count() == 2);

  const std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  const auto groups = from(v).groupBy([](int x) { return x % 2; }).toStdVector();
  CHECK(groups.size() == 2);
  CHECK(groups.size() == 2 && groups[0].first == 1 && groups[1].first == 0);
  CHECK(groups.size() == 2 && groups[0].second.sum() == 25);
  CHECK(groups.size() == 2 && (groups[1].second.toStdVector() ==
                               std::vector<int>{2, 4, 6, 8, 10}));

  const std::vector<int> dups = {3, 1, 3, 2, 1};
  CHECK((from(dups).distinct().toStdVector() == std::vector<int>{3, 1, 2}));
  CHECK((from(dups).orderBy().toStdVector() == std::vector<int>{1, 1, 2, 3, 3}));
  CHECK((from(dups).toStdSet() == std::set<int>{1, 2, 3}));
  CHECK(from(dups).count(3) == 2);
}

void test_sources_and_end() {
  // Non-const: from(T (&)[N]) would make the element type const.
  int array[] = {4, 5, 6};
  const std::list<int> list = {4, 5, 6};
  const std::deque<int> deque = {4, 5, 6};
  const std::set<int> set = {6, 5, 4};
  const std::array<int, 3> std_array = {{4, 5, 6}};
  const std::vector<int> expected = {4, 5, 6};
  CHECK(from(array).toStdVector() == expected);
  CHECK(from(list).toStdVector() == expected);
  CHECK(from(deque).toStdVector() == expected);
  CHECK(from(set).toStdVector() == expected);
  CHECK(from(std_array).toStdVector() == expected);
  CHECK((from(&array[0], 2).toStdVector() == std::vector<int>{4, 5}));
  CHECK((from(list).toStdDeque() == std::deque<int>{4, 5, 6}));
  CHECK((from(deque).toStdList() == std::list<int>{4, 5, 6}));

  CHECK((boolinq::range(0, 10, 3).toStdVector() == std::vector<int>{0, 3, 6, 9}));
  CHECK(boolinq::repeat(std::string("ab"), 3).sum() == "ababab");

  const std::vector<int> empty;
  bool threw = false;
  try {
    from(empty).first();
  } catch (const boolinq::LinqEndException&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(from(empty).firstOrDefault(-1) == -1);
  CHECK(from(empty).lastOrDefault() == 0);
  CHECK(!from(empty).any());
}

void test_bytes_and_bits() {
  const std::vector<int> ints = {0x12345678, -2};
  unsigned char raw[sizeof(int)];
  std::memcpy(raw, &ints[0], sizeof raw);

  const std::vector<int> bytes = from(ints).bytes().toStdVector();
  CHECK(bytes.size() == 2 * sizeof(int));
  bool same_order = bytes.size() == 2 * sizeof(int);
  for (std::size_t i = 0; same_order && i < sizeof(int); ++i) {
    same_order = bytes[i] == raw[i];
  }
  CHECK(same_order);
  const std::vector<int> reversed =
      from(ints).take(1).bytes(boolinq::BytesLastToFirst).toStdVector();
  CHECK(reversed.size() == sizeof(int) && reversed[0] == raw[sizeof(int) - 1]);
  CHECK(from(ints).bytes().unbytes<int>().toStdVector() == ints);

  const std::vector<unsigned char> octet = {0x1D};
  CHECK((from(octet).bits().toStdVector() == std::vector<int>{0, 0, 0, 1, 1, 1, 0, 1}));
  CHECK((from(octet).bits(boolinq::BitsLowToHigh).toStdVector() ==
         std::vector<int>{1, 0, 1, 1, 1, 0, 0, 0}));
  CHECK(from(octet).bits().unbits().toStdVector() == octet);
  CHECK(from(ints).bits().unbits().unbytes<int>().toStdVector() == ints);
}

}  // namespace

int main() {
  test_filters_and_transforms();
  test_structs_and_grouping();
  test_sources_and_end();
  test_bytes_and_bits();
  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("boolinq: ok\n");
  return 0;
}
