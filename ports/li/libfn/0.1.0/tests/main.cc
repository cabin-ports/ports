#include <fn/and_then.hpp>
#include <fn/choice.hpp>
#include <fn/discard.hpp>
#include <fn/expected.hpp>
#include <fn/fail.hpp>
#include <fn/filter.hpp>
#include <fn/inspect.hpp>
#include <fn/inspect_error.hpp>
#include <fn/optional.hpp>
#include <fn/or_else.hpp>
#include <fn/pack.hpp>
#include <fn/recover.hpp>
#include <fn/transform.hpp>
#include <fn/transform_error.hpp>
#include <fn/utility.hpp>
#include <fn/value_or.hpp>
#include <libfn_version.hpp>
#include <pfn/expected.hpp>
#include <pfn/optional.hpp>

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

#ifndef LIBFN_VERSION
#error LIBFN_VERSION is not defined
#endif

namespace app {

// Error alternatives live in a named namespace: libfn orders types by name.
enum class NotANumber {};
enum class DivByZero {};

struct Error {
  std::string what;
};

auto parse(std::string_view s) -> fn::expected<int, fn::copack<NotANumber>> {
  if (s.empty()) return fn::unexpected{fn::copack{NotANumber{}}};
  int value = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return fn::unexpected{fn::copack{NotANumber{}}};
    value = value * 10 + (c - '0');
  }
  return value;
}

auto divide(int a, int b) -> fn::expected<int, fn::copack<DivByZero>> {
  if (b == 0) return fn::unexpected{fn::copack{DivByZero{}}};
  return a / b;
}

// operator& gathers both operands into a pack, which and_then spreads into
// divide's two parameters; the error types fold into one copack.
auto quotient(std::string_view a, std::string_view b) {
  return (parse(a) & parse(b)) | fn::and_then(divide);
}

static_assert(std::is_same_v<decltype(quotient("", "")),
                             fn::expected<int, fn::copack_for<DivByZero, NotANumber>>>);

}  // namespace app

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void test_copack_errors() {
  CHECK(app::quotient("84", "2").value() == 42);
  CHECK(app::quotient("84", "x").error().has_value<app::NotANumber>());
  CHECK(app::quotient("1", "0").error().has_value<app::DivByZero>());
  CHECK(!app::quotient("1", "0").error().has_value<app::NotANumber>());
  // & keeps the leftmost error, and divide never runs.
  CHECK(app::quotient("x", "0").error().has_value<app::NotANumber>());
  CHECK(app::quotient("1", "0").error().apply(fn::overload{[](app::DivByZero) { return 1; },  //
                                                           [](app::NotANumber) { return 2; }}) == 1);
}

void test_expected_pipeline() {
  using E = fn::expected<int, app::Error>;
  int seen = 0;
  std::string error_seen;
  const auto run = [&](E start) {
    return start                                                                  //
           | fn::filter([](int v) { return v >= 0; }, [](int) { return app::Error{"negative"}; })
           | fn::and_then([](int v) -> E { return v * 2; })                       //
           | fn::inspect([&](int v) { seen = v; })                                //
           | fn::inspect_error([&](app::Error const& e) { error_seen = e.what; }) //
           | fn::transform([](int v) { return v + 1; })                           //
           | fn::transform_error([](app::Error e) { return e.what.size(); });
  };
  const auto ok = run(E{20});
  static_assert(std::is_same_v<std::remove_const_t<decltype(ok)>, fn::expected<int, std::size_t>>);
  CHECK(ok.value() == 41 && seen == 40 && error_seen.empty());

  const auto bad = run(E{-3});
  CHECK(!bad.has_value() && bad.error() == 8 && error_seen == "negative" && seen == 40);

  const auto failed = E{5} | fn::fail([](int) { return app::Error{"refused"}; });
  CHECK(!failed.has_value() && failed.error().what == "refused");

  const auto recovered = failed | fn::recover([](app::Error const&) { return -1; });
  CHECK(recovered.value() == -1);

  const auto rescued = failed | fn::or_else([](app::Error const& e) -> E { return static_cast<int>(e.what.size()); });
  CHECK(rescued.value() == 7);

  // discard ends a pipeline that runs for its side effects.
  static_assert(std::is_void_v<decltype(E{1} | fn::discard())>);
  int inspected = 0;
  E{9} | fn::inspect([&](int v) { inspected = v; }) | fn::discard();
  CHECK(inspected == 9);
}

// Carrier | carrier: the leftmost value wins; with no value, every error is kept.
void test_disjunction() {
  using E = fn::expected<int, app::Error>;
  const E bad1{fn::unexpect, app::Error{"a"}};
  const E bad2{fn::unexpect, app::Error{"b"}};
  CHECK((E{1} | E{2}).value() == 1);
  CHECK((bad1 | E{2}).value() == 2);
  const auto both = bad1 | bad2;
  CHECK(!both.has_value() && fn::get<0>(both.error()).what == "a" && fn::get<1>(both.error()).what == "b");
  CHECK((bad1 | bad2 | E{3}).value() == 3);

  const auto joined = fn::conjoin(E{4}, E{5});
  CHECK(joined.value().apply([](int a, int b) { return a * b; }) == 20);
  CHECK(fn::conjoin(E{4}, bad2).error().what == "b");
}

void test_optional() {
  const auto half = [](int v) -> fn::optional<int> {
    if (v % 2 != 0) return {};
    return v / 2;
  };
  CHECK((fn::optional<int>{8} | fn::and_then(half) | fn::and_then(half)).value() == 2);
  CHECK(!(fn::optional<int>{6} | fn::and_then(half) | fn::and_then(half)).has_value());
  CHECK(!(fn::optional<int>{12} | fn::filter([](int v) { return v > 20; })).has_value());
  CHECK((fn::optional<int>{} | fn::or_else([]() -> fn::optional<int> { return 9; })).value() == 9);
  CHECK((fn::optional<int>{3} | fn::transform([](int v) { return std::to_string(v); })).value() == "3");
  CHECK((fn::optional<int>{} | fn::value_or(5)).value() == 5 && (fn::optional<int>{2} | fn::value_or(5)).value() == 2);

  int target = 1;
  pfn::optional<int&> ref{target};
  *ref = 5;
  CHECK(target == 5 && &ref.value() == &target);

  int visits = 0;
  for (int v : pfn::optional<int>{4}) visits += v;
  for (int v : pfn::optional<int>{}) visits += v;
  CHECK(visits == 4);
}

void test_pack_and_choice() {
  const auto p = fn::as_pack(1, std::string("two"), 3.5);
  CHECK(fn::get<0>(p) == 1 && fn::get<1>(p) == "two" && fn::get<2>(p) == 3.5);
  CHECK(p.apply([](int a, std::string const& b, double c) { return a + static_cast<int>(b.size()) + c; }) == 7.5);

  using shape = fn::choice_for<double, int>;
  const shape s{3};
  CHECK(s.has_value<int>() && !s.has_value<double>() && *s.value().get_ptr<int>() == 3);
  const auto twice = fn::overload{[](int i) { return i * 2; }, [](double d) { return static_cast<int>(d * 2); }};
  const auto doubled = s | fn::transform(twice);
  // Both arms return int, so the result collapses to a single alternative.
  static_assert(std::is_same_v<std::remove_const_t<decltype(doubled)>, fn::choice<int>>);
  CHECK(*doubled.value().get_ptr<int>() == 6);
  CHECK(*(shape{2.5} | fn::transform(twice)).value().get_ptr<int>() == 5);
}

void test_polyfills() {
  pfn::expected<int, std::string> e{5};
  CHECK(e.value() == 5 && e.transform([](int v) { return v * 3; }).value() == 15);
  pfn::expected<int, std::string> u{pfn::unexpect, "no"};
  CHECK(!u.has_value() && u.error() == "no" && u.value_or(4) == 4);
  CHECK(u.or_else([](std::string const& s) -> pfn::expected<int, std::string> {
             return static_cast<int>(s.size());
           }).value() == 2);

  pfn::optional<int> o{};
  CHECK(!o.has_value() && o.value_or(7) == 7);
  o = 4;
  CHECK(o.and_then([](int v) -> pfn::optional<int> { return v + 1; }).value() == 5);
}

}  // namespace

int main() {
  test_copack_errors();
  test_expected_pipeline();
  test_disjunction();
  test_optional();
  test_pack_and_choice();
  test_polyfills();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("libfn 0.1.0 ok");
  return 0;
}
