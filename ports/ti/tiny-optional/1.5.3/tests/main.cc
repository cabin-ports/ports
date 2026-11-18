// tiny::optional as a std::optional replacement: the empty state stored in
// unused bits of double, float, bool and pointers, or in a member (sizes
// pinned on x86 and x64 only), sentinels, optional_aip,
// optional_sentinel_via_type, a custom optional_flag_manipulator (the README's
// IndexPair), and std::optional's API, all with exact values.

// The default mode exploits unused bits only on x86 and x64 and #errors on any
// other architecture, where consumers define this macro (see cabin.toml).
#if !(defined(__x86_64__) || defined(_M_X64) || defined(i386) || \
      defined(__i386__) || defined(__i386) || defined(_M_IX86))
#define TINY_OPTIONAL_USE_SEPARATE_BOOL_INSTEAD_OF_UB_TRICKS
#endif

#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

// As the README advises, the specialization below sits next to its type and
// sees only the forward declaration; <tiny/optional.h> comes after it.
#include <tiny/optional_flag_manipulator_fwd.h>

static int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("main.cc:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

namespace demo {

struct Data {
  int count;
  double weight;
};

// The README's IndexPair: both indices are never negative at the same time.
class IndexPair {
 public:
  IndexPair(int first, int second) : first_(first), second_(second) {}
  int first() const { return first_; }
  int second() const { return second_; }

 private:
  int first_;
  int second_;
};

struct MinusOne {
  static constexpr double value = -1.0;
};

}  // namespace demo

template <>
struct tiny::optional_flag_manipulator<demo::IndexPair> {
  static bool is_empty(const demo::IndexPair& payload) noexcept {
    return payload.first() < 0 && payload.second() < 0;
  }
  static void init_empty_flag(demo::IndexPair& memory) noexcept {
    ::new (&memory) demo::IndexPair(-1, -1);
  }
  static void invalidate_empty_flag(demo::IndexPair& empty) noexcept {
    empty.~IndexPair();
  }
};

#include <tiny/optional.h>

// NaN, infinity, false and nullptr stay values, never the empty state.
static void unusedBits() {
  tiny::optional<double> d;
  CHECK(!d.has_value() && d == std::nullopt && d.value_or(2.5) == 2.5);
  d = std::numeric_limits<double>::quiet_NaN();
  CHECK(d.has_value() && std::isnan(*d));
  d = -std::numeric_limits<double>::infinity();
  CHECK(d.has_value() && std::isinf(*d) && *d < 0);
  d = 0.125;
  CHECK(d && *d == 0.125 && d.value_or(2.5) == 0.125);
  d.reset();
  CHECK(!d);

  // Copies keep the value and the empty state, by construction and assignment.
  const tiny::optional<double> half = 0.5, none = std::nullopt;
  tiny::optional<double> copy(half), emptyCopy(none);
  CHECK(copy && *copy == 0.5 && !emptyCopy);
  copy = none;
  emptyCopy = half;
  CHECK(!copy && emptyCopy && *emptyCopy == 0.5);
  tiny::optional<double> quarter = 0.25;
  quarter = emptyCopy;
  CHECK(quarter && *quarter == 0.5);

  tiny::optional<float> f = 1.5f;
  CHECK(f && *f == 1.5f);
  f = std::numeric_limits<float>::quiet_NaN();
  CHECK(f && std::isnan(*f));
  f = std::nullopt;
  CHECK(!f);

  tiny::optional<bool> b = false;
  CHECK(b.has_value() && !*b);
  b = std::nullopt;
  CHECK(!b.has_value());
  b = true;
  CHECK(b.has_value() && *b);

  int x = 7;
  tiny::optional<int*> p = nullptr;
  CHECK(p.has_value() && *p == nullptr);
  p = &x;
  CHECK(p && **p == 7);
  p.reset();
  CHECK(!p);

  // The empty flag lives in Data::weight, or in Data::count as -1.
  tiny::optional<demo::Data, &demo::Data::weight> inWeight;
  CHECK(!inWeight);
  inWeight = demo::Data{3, std::numeric_limits<double>::quiet_NaN()};
  CHECK(inWeight && inWeight->count == 3 && std::isnan(inWeight->weight));
  const auto weightCopy = inWeight;
  CHECK(weightCopy && weightCopy->count == 3 && std::isnan(weightCopy->weight));
  inWeight.reset();
  CHECK(!inWeight);
  const auto emptyWeightCopy = inWeight;
  CHECK(!emptyWeightCopy);
  tiny::optional<demo::Data, &demo::Data::count, -1> inCount;
  CHECK(!inCount);
  inCount = demo::Data{0, -0.5};
  CHECK(inCount && inCount->count == 0 && inCount->weight == -0.5);
  inCount = std::nullopt;
  CHECK(!inCount);

#ifndef TINY_OPTIONAL_USE_SEPARATE_BOOL_INSTEAD_OF_UB_TRICKS
  CHECK(sizeof(tiny::optional<double>) == sizeof(double) &&
        tiny::optional<double>::is_compressed);
  CHECK(sizeof(tiny::optional<float>) == sizeof(float) &&
        sizeof(tiny::optional<bool>) == sizeof(bool) &&
        sizeof(tiny::optional<int*>) == sizeof(int*));
  CHECK(sizeof(inWeight) == sizeof(demo::Data) &&
        sizeof(inCount) == sizeof(demo::Data));
#else
  // The macro gives these optionals a separate bool, as std::optional has.
  CHECK(sizeof(tiny::optional<double>) == sizeof(std::optional<double>) &&
        !tiny::optional<double>::is_compressed);
  CHECK(sizeof(tiny::optional<bool>) == sizeof(std::optional<bool>) &&
        sizeof(tiny::optional<int*>) == sizeof(std::optional<int*>));
  CHECK(sizeof(inWeight) == sizeof(std::optional<demo::Data>));
#endif
}

// Sentinels and the custom flag manipulator need no platform tricks.
static void sentinels() {
  tiny::optional<int, -1> index;
  CHECK(sizeof(index) == sizeof(int) && decltype(index)::is_compressed);
  CHECK(!index && index.value_or(42) == 42);
  index = 0;
  CHECK(index.has_value() && *index == 0);
  CHECK(index.emplace(17) == 17 && index.value() == 17);
  index = std::nullopt;
  CHECK(!index.has_value());

  auto made = tiny::make_optional<unsigned int, 0u>(42u);
  CHECK((std::is_same_v<decltype(made), tiny::optional<unsigned int, 0u>>));
  CHECK(made && *made == 42u && sizeof(made) == sizeof(unsigned int));

  CHECK((std::is_same_v<tiny::optional_aip<int>, tiny::optional<int, INT_MIN>>));
  CHECK((std::is_same_v<tiny::optional_aip<unsigned>,
                        tiny::optional<unsigned, UINT_MAX>>));
  tiny::optional_aip<int> aip;
  CHECK(sizeof(aip) == sizeof(int) && !aip);
  aip = INT_MAX;
  CHECK(aip && *aip == INT_MAX);
  aip = INT_MIN + 1;
  CHECK(aip && *aip == INT_MIN + 1);
  tiny::optional_aip<char, 'z'> letter = 'a';
  CHECK(sizeof(letter) == 1 && letter && *letter == 'a');

  tiny::optional_sentinel_via_type<double, demo::MinusOne> viaType;
  CHECK(sizeof(viaType) == sizeof(double) && !viaType);
  viaType = 0.25;
  CHECK(viaType && *viaType == 0.25);
  viaType = std::numeric_limits<double>::quiet_NaN();
  CHECK(viaType && std::isnan(*viaType));

  tiny::optional<demo::IndexPair> pair;
  CHECK(sizeof(pair) == sizeof(demo::IndexPair) &&
        tiny::optional<demo::IndexPair>::is_compressed && !pair);
  pair.emplace(-1, 5);
  CHECK(pair && pair->first() == -1 && pair->second() == 5);
  pair.reset();
  CHECK(!pair);
  pair = demo::IndexPair(2, -3);
  CHECK(pair && pair->first() == 2 && pair->second() == -3);
  const auto pairCopy = pair;
  CHECK(pairCopy && pairCopy->first() == 2 && pairCopy->second() == -3);
  CHECK(sizeof(tiny::optional_aip<demo::IndexPair>) == sizeof(demo::IndexPair));
}

// The std::optional API on payloads without a tiny state.
static void stdApi() {
  using Index = tiny::optional<int, -1>;
  CHECK(sizeof(tiny::optional<int>) == sizeof(std::optional<int>) &&
        !tiny::optional<int>::is_compressed);
  CHECK(tiny::is_tiny_optional_v<Index> &&
        !tiny::is_tiny_optional_v<std::optional<int>>);
  CHECK(TINY_OPTIONAL_VERSION == 100503);
  tiny::optional deduced{42};
  CHECK((std::is_same_v<decltype(deduced), tiny::optional<int>>));
  CHECK(deduced && *deduced == 42);

  tiny::optional<std::string> s;
  bool threw = false;
  try {
    (void)s.value();
  } catch (const std::bad_optional_access&) {
    threw = true;
  }
  CHECK(threw && !s);
  s = std::string("tiny");
  CHECK(s.value() == "tiny" && s->size() == 4 && *s == "tiny");
  s->append("-optional");
  tiny::optional<std::string> other;
  other.swap(s);
  CHECK(!s && other && *other == "tiny-optional");
  tiny::optional<std::string> moved = std::move(other);
  CHECK(moved && *moved == "tiny-optional" && other.has_value());
  tiny::optional<std::string> copied;
  copied = moved;
  CHECK(copied && *copied == "tiny-optional" && *moved == "tiny-optional");
  const tiny::optional<std::string> noString = std::nullopt;
  copied = noString;
  CHECK(!copied);
  CHECK(s.emplace(3, 'x') == "xxx" && *s == "xxx");

  Index one = 1, two = 2, none;
  CHECK(one < two && none < one && one <= one && two >= one && two > one);
  CHECK(one == 1 && 1 == one && one != two && none != 0 && !(none > one));
  CHECK(!(one < one) && !(one > one) && one >= none && !(none >= one));
  CHECK(!(none == 0) && !(0 == none) && none <= one && !(one <= none));
  CHECK(none == std::nullopt && std::nullopt != one);
  CHECK(one == std::optional<int>(1) && none == std::optional<int>());
  CHECK(std::hash<Index>{}(one) == std::hash<int>{}(1) &&
        std::hash<Index>{}(none) == 0);

  auto half = [](int v) {
    return v % 2 == 0 ? tiny::optional<int>(v / 2) : tiny::optional<int>();
  };
  tiny::optional<int> four = 4, three = 3, empty;
  CHECK(four.and_then(half) == 2 && !three.and_then(half) &&
        !empty.and_then(half));
  auto length = moved.transform([](const std::string& v) { return v.size(); });
  CHECK((std::is_same_v<decltype(length), tiny::optional<std::size_t>>));
  CHECK(length && *length == 13);
  CHECK(!tiny::optional<std::string>().transform(
            [](const std::string& v) { return v.size(); }));
}

int main() {
  unusedBits();
  sentinels();
  stdApi();
  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("tiny-optional: ok");
  return 0;
}
