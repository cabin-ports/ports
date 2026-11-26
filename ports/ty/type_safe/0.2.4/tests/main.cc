// Consumer test for cabin-ports/type_safe: strong_typedef operators,
// overflow-checked integers, optional and optional_ref, flag and boolean.
#include <type_safe/boolean.hpp>
#include <type_safe/flag.hpp>
#include <type_safe/integer.hpp>
#include <type_safe/optional.hpp>
#include <type_safe/optional_ref.hpp>
#include <type_safe/strong_typedef.hpp>
#include <type_safe/types.hpp>

#include <cstdio>
#include <stdexcept>

namespace ts = type_safe;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

struct distance
    : ts::strong_typedef<distance, int>,
      ts::strong_typedef_op::equality_comparison<distance>,
      ts::strong_typedef_op::relational_comparison<distance>,
      ts::strong_typedef_op::integer_arithmetic<distance> {
  using strong_typedef::strong_typedef;
};

using checked_int = ts::integer<int, ts::checked_arithmetic>;

}  // namespace

int main() {
  // strong_typedef: arithmetic and comparison operators.
  distance a(7), b(3);
  check(static_cast<int>(a + b) == 10, "strong add");
  check(static_cast<int>(a - b) == 4, "strong sub");
  check(static_cast<int>(a * b) == 21, "strong mul");
  check(static_cast<int>(a / b) == 2, "strong div");
  check(static_cast<int>(a % b) == 1, "strong mod");
  check(a != b && !(a == b) && a == distance(7), "strong equality");
  check(b < a && a > b && b <= b && a >= a, "strong relational");
  a += b;
  check(static_cast<int>(a) == 10, "strong compound assignment");

  // checked integers: results and overflow detection.
  checked_int x(2147483600);
  check(static_cast<int>(x + checked_int(47)) == 2147483647, "checked add at max");
  bool add_threw = false;
  try {
    (void)(x + checked_int(100));
  } catch (const ts::checked_arithmetic::error&) {
    add_threw = true;
  }
  check(add_threw, "checked add overflow throws");
  bool div_threw = false;
  try {
    (void)(checked_int(1) / checked_int(0));
  } catch (const std::range_error&) {
    div_threw = true;
  }
  check(div_threw, "checked division by zero throws");
  check(static_cast<int>(checked_int(-5) * checked_int(4)) == -20, "checked mul");
  check(ts::abs(checked_int(-9)) == 9u, "integer abs");
  ts::uint32_t u(5u);
  check(static_cast<unsigned>(u - ts::uint32_t(2u)) == 3u, "types.hpp uint32_t");

  // optional.
  ts::optional<int> none;
  check(!none.has_value(), "empty optional");
  check(none.value_or(42) == 42, "value_or on empty");
  auto some = ts::make_optional(5);
  check(some.has_value() && some.value() == 5, "optional value");
  auto doubled = some.map([](int v) { return v * 2; });
  check(doubled.has_value() && doubled.value() == 10, "optional map");
  check(!none.map([](int v) { return v * 2; }).has_value(), "map on empty");
  some.reset();
  check(!some.has_value(), "optional reset");

  // optional_ref.
  int target = 11;
  ts::optional_ref<int> ref = ts::opt_ref(target);
  check(ref.has_value() && ref.value() == 11, "optional_ref value");
  ref.value() = 12;
  check(target == 12, "optional_ref writes through");
  ts::optional_ref<int> null_ref;
  check(!null_ref.has_value(), "empty optional_ref");
  int fallback = -1;
  check(ref.value_or(fallback) == 12 && null_ref.value_or(fallback) == -1, "optional_ref value_or");

  // flag.
  ts::flag f(false);
  check(f == false, "flag initial");
  check(f.try_set() && f == true, "flag try_set changes");
  check(!f.try_set(), "flag try_set unchanged");
  check(f.toggle() && f == false, "flag toggle returns old state");
  f.set();
  check(f.try_reset() && f == false, "flag try_reset");

  // boolean: explicit-only conversions from bool.
  ts::boolean t(true), nt(false);
  check(t && !nt, "boolean values");
  check(static_cast<bool>(t || nt) && !static_cast<bool>(t && nt), "boolean logic");
  check(!t == nt, "boolean negation");

  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}
