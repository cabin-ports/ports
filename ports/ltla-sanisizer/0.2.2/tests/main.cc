// Consumer test for cabin-ports/ltla-sanisizer: checked casts, container
// creation through a container's size type, capping, overflow-checked sums
// and products (with attestations that remove the checks at compile time),
// size comparisons, N-dimensional offsets, iterator difference limits,
// float conversions, and the Cast and Exact wrappers, through the umbrella
// header.
#include <sanisizer/sanisizer.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

template <class Error, class Fn> bool throws(Fn fn) {
  try {
    fn();
  } catch (const Error &) {
    return true;
  }
  return false;
}

// A container whose size type is std::uint8_t.
struct SmallBox {
  std::uint8_t n = 0;
  SmallBox() = default;
  explicit SmallBox(std::uint8_t count) : n(count) {}
  std::uint8_t size() const { return n; }
  void resize(std::uint8_t count) { n = count; }
};

// An iterator whose difference type is std::int8_t.
struct TinyIterator {
  int pos;
};
std::int8_t operator-(TinyIterator a, TinyIterator b) {
  return static_cast<std::int8_t>(a.pos - b.pos);
}

std::uint8_t take_u8(std::uint8_t x) { return x; }

static_assert(sanisizer::cast<std::uint16_t>(65535) == 65535, "constexpr cast");
static_assert(sanisizer::sum<std::uint8_t>(100, 155) == 255, "constexpr sum");
static_assert(sanisizer::cap<std::int8_t>(1000) == 127, "constexpr cap");

void test_cast_and_create() {
  check(sanisizer::cast<std::uint8_t>(255LL) == 255, "cast within range");
  check(sanisizer::can_cast<std::uint8_t>(200) == 200, "can_cast keeps the value");
  check(throws<std::overflow_error>([] { sanisizer::can_cast<std::uint8_t>(300); }),
        "can_cast overflow throws");
  static_assert(std::is_same<decltype(sanisizer::as_size_type<std::vector<int>>(10)),
                             std::vector<int>::size_type>::value,
                "as_size_type yields the container's size type");
  check(throws<std::overflow_error>([] { sanisizer::cast<std::uint8_t>(256); }),
        "cast overflow throws");
  check(throws<std::overflow_error>(
            [] { sanisizer::cast<std::int32_t>(std::uint64_t(1) << 40); }),
        "cast to a narrower signed type throws");

  const auto values = sanisizer::create<std::vector<double>>(3LL, 1.5);
  check(values.size() == 3 && values[0] == 1.5 && values[2] == 1.5, "create with a value");
  std::vector<int> grown;
  sanisizer::resize(grown, std::int16_t(5), 7);
  sanisizer::reserve(grown, 100u);
  check(grown.size() == 5 && grown[4] == 7 && grown.capacity() >= 100, "resize and reserve");

  check(sanisizer::create<SmallBox>(200).size() == 200, "create through a small size type");
  check(throws<std::overflow_error>([] { sanisizer::create<SmallBox>(300); }),
        "create overflow of the container's size type");
  SmallBox box;
  check(throws<std::overflow_error>([&box] { sanisizer::resize(box, 256); }) && box.size() == 0,
        "resize overflow leaves the container alone");
}

void test_cap() {
  check(sanisizer::cap<std::uint8_t>(1000) == 255, "cap at the type maximum");
  check(sanisizer::cap<std::uint8_t>(17) == 17, "cap keeps small values");
  check(sanisizer::cap<std::int16_t>(std::uint64_t(1) << 40) == 32767, "cap to a signed type");
}

void test_arithmetic() {
  check(sanisizer::sum<std::uint8_t>(100, 155) == 255, "sum at the limit");
  check(throws<std::overflow_error>([] { sanisizer::sum<std::uint8_t>(100, 156); }),
        "sum overflow throws");
  check(sanisizer::sum<std::uint16_t>(1, 2, 3, 4) == 10, "variadic sum");
  check(throws<std::overflow_error>([] { sanisizer::sum<std::uint8_t>(100, 100, 100); }),
        "variadic sum overflow throws");
  check(sanisizer::sum_unsafe<std::uint8_t>(5u, 20, 100) == 125, "unchecked sum");

  check(sanisizer::product<std::uint16_t>(255, 257) == 65535, "product at the limit");
  check(throws<std::overflow_error>([] { sanisizer::product<std::uint16_t>(256, 256); }),
        "product overflow throws");
  check(sanisizer::product<std::uint64_t>(123, 456, 789) == 44253432, "variadic product");
  check(sanisizer::product<std::uint8_t>(0, 200) == 0, "product with zero");
  check(sanisizer::product_unsafe<std::int64_t>(5, 20u, 100) == 10000, "unchecked product");
}

void test_attestations() {
  const std::size_t x = 99, y = 100;
  const auto ax = sanisizer::attest_max<int, 100>(x);
  const auto ay = sanisizer::attest_max<int, 100>(y);
  using A = std::remove_cv_t<decltype(ax)>;
  static_assert(sanisizer::is_Attestation<A>::value && A::max == 100, "attestation type");
  // A type already within the attested maximum comes back unwrapped.
  static_assert(std::is_same<decltype(sanisizer::attest_max<int, 1000>(std::uint8_t(7))),
                             std::uint8_t>::value,
                "attest_max of a narrow type");
  check(sanisizer::sum<std::uint8_t>(ax, ay) == 199, "sum of attestations");
  check(sanisizer::product<std::uint16_t>(ax, ay) == 9900, "product of attestations");

  // An attested maximum within the destination skips the overflow check.
  const auto bounded = sanisizer::attest_max_by_type<std::int32_t>(std::size_t(12345));
  check(sanisizer::cast<std::int32_t>(bounded) == 12345, "cast of an attestation");
  check(sanisizer::create<std::vector<char>>(sanisizer::Attestation<int, 20>(10)).size() == 10,
        "create from an attestation");
}

void test_comparisons_and_offsets() {
  check(sanisizer::is_equal(std::uint8_t(5), 5LL), "is_equal across types");
  check(sanisizer::is_less_than(std::int8_t(100), std::uint64_t(1) << 40), "is_less_than");
  check(sanisizer::is_greater_than_or_equal(std::uint64_t(300), std::uint8_t(255)),
        "is_greater_than_or_equal");
  check(sanisizer::is_less_than_or_equal(std::uint8_t(255), 255LL) &&
            sanisizer::is_greater_than(std::uint64_t(256), std::uint8_t(255)),
        "is_less_than_or_equal and is_greater_than");
  // min returns the narrower type, max the wider one.
  const auto lo = sanisizer::min(std::uint8_t(200), std::uint64_t(5));
  const auto hi = sanisizer::max(std::uint8_t(200), std::uint64_t(5));
  static_assert(std::is_same<std::remove_cv_t<decltype(lo)>, std::uint8_t>::value, "min type");
  static_assert(std::is_same<std::remove_cv_t<decltype(hi)>, std::uint64_t>::value, "max type");
  check(lo == 5 && hi == 200, "min and max");

  // x1 + d1 * (x2 + d2 * x3), computed in the size type.
  check(sanisizer::nd_offset<std::size_t>(3, 10, 4, 20, 5) == 1043, "3-D offset");
  // 100000 * 100000 overflows a 32-bit int; nd_offset multiplies in the size type.
  const std::int32_t big = 100000;
  check(sanisizer::nd_offset<std::uint64_t>(std::int32_t(1), big, big) == 10000000001u,
        "offset beyond the index type");

  check(sanisizer::can_ptrdiff<TinyIterator>(127), "difference within the iterator type");
  check(throws<std::overflow_error>([] { sanisizer::can_ptrdiff<TinyIterator>(128); }),
        "difference beyond the iterator type");
  check(sanisizer::can_ptrdiff<double *>(1000), "pointer differences");
}

// The float boundaries below assume IEEE-754 binary32 and binary64, as
// upstream's float tests do.
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
              "IEEE-754 floats");

void test_float() {
  check(sanisizer::required_bits_for_float(0.0) == 0 &&
            sanisizer::required_bits_for_float(1.0) == 1 &&
            sanisizer::required_bits_for_float(255.0) == 8 &&
            sanisizer::required_bits_for_float(256.0) == 9,
        "required_bits_for_float");
  check(sanisizer::from_float<std::uint8_t>(255.9) == 255, "from_float truncates");
  check(throws<std::overflow_error>([] { sanisizer::from_float<std::uint8_t>(256.0); }),
        "from_float overflow");
  check(throws<std::overflow_error>([] { sanisizer::from_float<std::int8_t>(128.0); }),
        "from_float overflow of a signed type");
  check(throws<std::out_of_range>([] { sanisizer::from_float<int>(-1.0); }),
        "from_float of a negative value");
  check(throws<std::range_error>(
            [] { sanisizer::from_float<int>(std::numeric_limits<double>::quiet_NaN()); }),
        "from_float of NaN");
  check(throws<std::range_error>(
            [] { sanisizer::from_float<int>(std::numeric_limits<double>::infinity()); }),
        "from_float of infinity");
  check(sanisizer::from_float<std::uint64_t>(1e19) == 10000000000000000000u, "from_float of 1e19");

  const std::uint64_t two53 = std::uint64_t(1) << 53;
  check(sanisizer::to_float<double>(two53) == 9007199254740992.0, "to_float exact");
  check(throws<std::overflow_error>([two53] { sanisizer::to_float<double>(two53 + 1); }),
        "to_float precision loss throws");
  check(sanisizer::to_float<float>(std::uint32_t(1) << 24) == 16777216.0f, "to_float to float");
  check(throws<std::overflow_error>(
            [] { sanisizer::to_float<float>((std::uint32_t(1) << 24) + 1); }),
        "to_float to float precision loss throws");
  check(sanisizer::to_float<double>(std::uint8_t(0)) == 0.0, "to_float of zero");
  check(sanisizer::to_float<double>(sanisizer::Attestation<std::uint64_t, 1000>(7)) == 7.0,
        "to_float of an attestation");
}

void test_wrappers() {
  check(take_u8(sanisizer::Cast(200)) == 200, "Cast converts on call");
  check(throws<std::overflow_error>([] { take_u8(sanisizer::Cast(256)); }), "Cast overflow throws");
  check(take_u8(sanisizer::Cast(sanisizer::attest_max<int, 10>(7))) == 7, "Cast of an attestation");
  check(take_u8(sanisizer::Exact(std::uint8_t(9))) == 9, "Exact passes its own type");
  // Exact refuses every other conversion at compile time.
  static_assert(std::is_convertible<sanisizer::Exact<std::uint8_t>, std::uint8_t>::value,
                "Exact to its own type");
  static_assert(!std::is_convertible<sanisizer::Exact<std::uint8_t>, int>::value,
                "Exact to another type");
}

}  // namespace

int main() {
  test_cast_and_create();
  test_cap();
  test_arithmetic();
  test_attestations();
  test_comparisons_and_offsets();
  test_float();
  test_wrappers();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ltla-sanisizer consumer ok");
  return 0;
}
