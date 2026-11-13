// Expected values follow from the inputs by hand; the and_then and eval checks
// also count calls, to check that the empty and error paths skip the callable.
#include <absent/absent.h>
#include <absent/adapters/either/and_then.h>
#include <absent/adapters/either/attempt.h>
#include <absent/adapters/either/eval.h>
#include <absent/adapters/either/for_each.h>
#include <absent/adapters/either/transform.h>
#include <absent/support/execution_status.h>
#include <absent/support/from_variant.h>
#include <absent/support/sink.h>

#include <cstdio>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace {

namespace ra = rvarago::absent;
namespace re = rvarago::absent::adapters::either;
using Either = rvarago::absent::adapters::types::either<int, std::string>;

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

// A run of 1 to 9 decimal digits.
std::optional<int> parse(std::string const &s) {
    if (s.empty() || s.size() > 9) return std::nullopt;
    int value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value*10 + (c - '0');
    }
    return value;
}

std::optional<int> half(int x) {
    if (x%2 != 0) return std::nullopt;
    return x/2;
}

Either halfOrError(int x) {
    if (x%2 != 0) return std::string("odd ") + std::to_string(x);
    return x/2;
}

std::string show(int x) {
    return "<" + std::to_string(x) + ">";
}

constexpr auto plusOne = [](int x) { return x + 1; };
constexpr auto zero = [] { return 0; };

// eval and transform are constexpr.
static_assert(ra::eval(ra::transform(std::optional<int>{20}, plusOne), zero) == 21);
static_assert(ra::eval(ra::transform(std::optional<int>{}, plusOne), zero) == 0);

void testOptional() {
    CHECK(ra::and_then(parse("84"), half) == std::optional<int>{42});
    CHECK(ra::and_then(parse("85"), half) == std::nullopt);
    CHECK(ra::and_then(parse("8x"), half) == std::nullopt);
    int calls = 0;
    auto countedHalf = [&calls](int x) {
        ++calls;
        return half(x);
    };
    CHECK(ra::and_then(std::optional<int>{}, countedHalf) == std::nullopt && calls == 0);
    CHECK(ra::and_then(std::optional<int>{8}, countedHalf) == std::optional<int>{4} && calls == 1);

    CHECK(ra::transform(parse("7"), show) == std::optional<std::string>{"<7>"});
    CHECK(ra::transform(parse(""), show) == std::nullopt);

    int fallbacks = 0;
    auto fallback = [&fallbacks] {
        ++fallbacks;
        return -1;
    };
    CHECK(ra::eval(parse("5"), fallback) == 5 && fallbacks == 0);
    CHECK(ra::eval(parse("5a"), fallback) == -1 && fallbacks == 1);

    std::string seen;
    auto record = [&seen](int x) { seen += show(x); };
    ra::for_each(parse("9"), record);
    ra::for_each(parse("-9"), record);
    ra::for_each(parse("10"), record);
    CHECK(seen == "<9><10>");

    CHECK(ra::attempt([] { return 3; }) == std::optional<int>{3});
    CHECK(ra::attempt([]() -> int { throw std::runtime_error("boom"); }) == std::nullopt);
    // The default BaseException is std::exception itself.
    CHECK(ra::attempt([]() -> int { throw std::exception(); }) == std::nullopt);
    // Only BaseException and what derives from it is caught.
    CHECK(ra::attempt<std::logic_error>([]() -> int { throw std::out_of_range("range"); }) == std::nullopt);
    bool escaped = false;
    try {
        std::optional<int> never = ra::attempt<std::logic_error>([]() -> int { throw std::runtime_error("boom"); });
        CHECK(!never);
    } catch (std::runtime_error const &e) {
        escaped = std::string(e.what()) == "boom";
    }
    CHECK(escaped);
}

void testOptionalOperators() {
    using ra::operator>>;
    using ra::operator|;
    CHECK((parse("168") >> half >> half) == std::optional<int>{42});
    CHECK((parse("84") >> half >> half >> half) == std::nullopt); // 21 is odd
    CHECK((parse("12") | plusOne | show) == std::optional<std::string>{"<13>"});
    CHECK((parse("1 2") | plusOne | show) == std::nullopt);
    // >> binds tighter than |, as in README.md's find_person() >> find_address | get_zip_code.
    CHECK((parse("84") >> half | show) == std::optional<std::string>{"<42>"});
    CHECK((parse("85") >> half | show) == std::nullopt);
}

void testSupport() {
    namespace rs = rvarago::absent::support;
    CHECK(rs::success.has_value() && !rs::failure.has_value());
    // blank has a single value: equal to itself, ordered neither way.
    CHECK(rs::unit == rs::blank{} && !(rs::unit != rs::unit));
    CHECK(!(rs::unit < rs::unit) && !(rs::unit > rs::unit) && rs::unit <= rs::unit && rs::unit >= rs::unit);
    CHECK(std::hash<rs::blank>{}(rs::unit) == std::hash<rs::blank>{}(rs::blank{}));

    auto seven = rs::sink([] { return 7; });
    CHECK(seven() == 7 && seven(1, "two", 3.0) == 7);
    CHECK(ra::transform(rs::success, rs::sink([] { return 1; })) == std::optional<int>{1});
    CHECK(ra::transform(rs::failure, rs::sink([] { return 1; })) == std::nullopt);

    std::variant<int, std::string> text = std::string("text"), number = 12;
    CHECK(ra::from_variant<std::string>(text) == std::optional<std::string>{"text"});
    CHECK(ra::from_variant<int>(text) == std::nullopt);
    CHECK(ra::from_variant<int>(number) == std::optional<int>{12});
}

void testEither() {
    const Either error = std::string("bad");
    CHECK(re::and_then(Either{84}, halfOrError) == Either{42});
    CHECK(re::and_then(Either{7}, halfOrError) == Either{std::string("odd 7")});
    int calls = 0;
    auto counted = [&calls](int x) {
        ++calls;
        return halfOrError(x);
    };
    CHECK(re::and_then(error, counted) == error && calls == 0);

    using Halved = rvarago::absent::adapters::types::either<double, std::string>;
    auto halve = [](int x) { return x*0.5; };
    CHECK(re::transform(Either{5}, halve) == Halved{2.5});
    CHECK(re::transform(error, halve) == Halved{std::string("bad")});

    CHECK(re::eval(Either{3}, zero) == 3);
    CHECK(re::eval(error, [] { return -5; }) == -5);

    std::string seen;
    auto record = [&seen](int x) { seen += show(x); };
    re::for_each(Either{1}, record);
    re::for_each(error, record);
    CHECK(seen == "<1>");

    auto fine = re::attempt([] { return 2; });
    static_assert(std::is_same_v<decltype(fine), rvarago::absent::adapters::types::either<int, std::exception>>);
    CHECK(std::get<int>(fine) == 2);
    auto caught = re::attempt([]() -> int { throw std::out_of_range("range"); });
    CHECK(std::holds_alternative<std::exception>(caught));
    auto thrown = re::attempt<std::runtime_error>([]() -> int { throw std::runtime_error("boom"); });
    CHECK(std::holds_alternative<std::runtime_error>(thrown) &&
          std::string(std::get<std::runtime_error>(thrown).what()) == "boom");
}

void testEitherOperators() {
    using re::operator>>;
    using re::operator|;
    CHECK((Either{168} >> halfOrError >> halfOrError) == Either{42});
    CHECK((Either{84} >> halfOrError >> halfOrError >> halfOrError) == Either{std::string("odd 21")});
    CHECK((Either{84} >> halfOrError | plusOne) == Either{43});
    CHECK((Either{std::string("bad")} >> halfOrError | plusOne) == Either{std::string("bad")});
}

} // namespace

int main() {
    testOptional();
    testOptionalOperators();
    testSupport();
    testEither();
    testEitherOperators();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("absent: ok\n");
    return 0;
}
