// The C++ consumer: tl::expected as a result type, in C++11 - values and
// tl::unexpected errors, the and_then/map/or_else/map_error chain, the void
// specialization, move-only and in-place payloads, assignment and swap
// across the two states with their destructors, comparisons,
// bad_expected_access, and constant expressions.
#include <tl/expected.hpp>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static bool ok = true;

static void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

enum class Error { empty, not_a_digit, too_large, odd };

// A non-negative decimal number of at most a million.
static tl::expected<int, Error> parse(const std::string &text) {
    if (text.empty()) return tl::make_unexpected(Error::empty);
    long value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return tl::make_unexpected(Error::not_a_digit);
        value = value * 10 + (c - '0');
        if (value > 1000000) return tl::make_unexpected(Error::too_large);
    }
    return static_cast<int>(value);
}

static tl::expected<int, Error> halve(int n) {
    if (n % 2 != 0) return tl::unexpected<Error>(Error::odd);
    return n / 2;
}

static tl::expected<void, std::string> positive(int n) {
    if (n > 0) return {};
    return tl::make_unexpected("not positive: " + std::to_string(n));
}

static void values_and_errors() {
    const tl::expected<int, Error> answer = parse("42");
    check(answer.has_value() && static_cast<bool>(answer) && *answer == 42 && answer.value() == 42,
          "a parsed number is the value");
    check(!parse("") && parse("").error() == Error::empty, "empty input is an error");
    check(parse("4x2").error() == Error::not_a_digit, "a stray letter is an error");
    check(parse("99999999").error() == Error::too_large, "an overflow is an error");
    check(parse("x").value_or(-1) == -1 && parse("7").value_or(-1) == 7 && answer.value_or(-1) == 42,
          "value_or falls back only on error");
}

static void monadic_chain() {
    int mapped = 0;
    const auto shout = [&mapped](int n) {
        ++mapped;
        return std::to_string(n) + "!";
    };
    const tl::expected<std::string, Error> even = parse("84").and_then(halve).map(shout);
    check(even && *even == "42!" && mapped == 1, "and_then and map run on success");
    const tl::expected<std::string, Error> odd = parse("85").and_then(halve).map(shout);
    check(!odd && odd.error() == Error::odd && mapped == 1, "an error skips map");
    check(parse("x").and_then(halve).error() == Error::not_a_digit, "the first error is kept");

    const tl::expected<int, int> code = parse("x").map_error([](Error e) { return static_cast<int>(e) * 10; });
    check(!code && code.error() == 10, "map_error converts the error");

    int recovered = 0;
    const auto recover = [&recovered](Error) -> tl::expected<int, Error> {
        ++recovered;
        return 0;
    };
    check(parse("").or_else(recover).value() == 0 && recovered == 1, "or_else recovers from an error");
    check(parse("5").or_else(recover).value() == 5 && recovered == 1, "or_else leaves a value alone");
}

static void void_results() {
    const tl::expected<void, std::string> yes = positive(3);
    const tl::expected<void, std::string> no = positive(-2);
    check(yes.has_value(), "expected<void> succeeds");
    check(!no && no.error() == "not positive: -2", "expected<void> carries the error");
    const tl::expected<int, std::string> next = yes.and_then([] { return tl::expected<int, std::string>(7); });
    check(next && *next == 7, "and_then continues from expected<void>");
    int calls = 0;
    const tl::expected<int, std::string> stopped = no.and_then([&calls] {
        ++calls;
        return tl::expected<int, std::string>(7);
    });
    check(!stopped && stopped.error() == "not positive: -2" && calls == 0, "and_then skips f on a void error");
    check(no.map([] { return 1; }).error() == "not positive: -2", "map keeps the void error");
}

static int alive = 0;

struct Tracked {
    int id;
    explicit Tracked(int i) : id(i) { ++alive; }
    Tracked(const Tracked &other) : id(other.id) { ++alive; }
    Tracked &operator=(const Tracked &) = default;
    ~Tracked() { --alive; }
};

static void payloads_and_states() {
    tl::expected<std::unique_ptr<int>, std::string> owned(std::unique_ptr<int>(new int(5)));
    tl::expected<std::unique_ptr<int>, std::string> moved(std::move(owned));
    check(moved && **moved == 5, "a move-only value moves");
    std::unique_ptr<int> taken = std::move(*moved);
    check(taken && *taken == 5, "the value moves out");

    const tl::expected<std::vector<int>, Error> list(tl::in_place, {1, 2, 3});
    check(list && list->size() == 3 && (*list)[2] == 3, "in-place construction from a list");
    const tl::expected<int, std::string> failed(tl::unexpect, 3, 'z');
    check(!failed && failed.error() == "zzz", "in-place construction of the error");

    {
        tl::expected<Tracked, int> slot(tl::in_place, 1);
        check(alive == 1, "the value is constructed once");
        slot = tl::make_unexpected(9);
        check(alive == 0 && slot.error() == 9, "assigning an error destroys the value");
        slot.emplace(2);
        check(alive == 1 && slot->id == 2, "emplace replaces the error");
        tl::expected<Tracked, int> other(tl::unexpect, 4);
        slot.swap(other);
        check(!slot && slot.error() == 4 && other && other->id == 2 && alive == 1, "swap exchanges the states");
        slot.swap(other);
        check(slot && slot->id == 2 && !other && other.error() == 4 && alive == 1, "swap from the error side");
        other = Tracked(3);
        check(other && other->id == 3 && alive == 2, "assigning a value replaces the error");
        other.emplace(4);
        check(other && other->id == 4 && alive == 2, "emplace replaces the value");
    }
    check(alive == 0, "every value is destroyed");

    // std::string moves without throwing, so swap takes its nothrow path.
    tl::expected<std::string, int> word("cabin"), code(tl::unexpect, 7), other_word("port");
    word.swap(code);
    check(!word && word.error() == 7 && code && *code == "cabin", "swap a nothrow value with an error");
    code.swap(other_word);
    check(*code == "port" && *other_word == "cabin", "swap two values");
    const tl::unexpected<int> eight(8);
    code = eight;
    check(!code && code.error() == 8, "assign a const unexpected");
    code.swap(word);
    check(code.error() == 7 && word.error() == 8, "swap two errors");
}

static void comparisons() {
    const tl::expected<int, Error> three(3);
    const tl::expected<int, Error> odd = halve(3);
    check(three == 3 && three != 4, "compare with a value");
    check(odd == tl::unexpected<Error>(Error::odd) && three != tl::unexpected<Error>(Error::odd),
          "compare with an unexpected");
    check(three != odd && halve(6) == three, "compare two expecteds");
    check(!(odd == 3) && odd == halve(5) && odd != parse("") && !(odd == parse("")), "compare errors");
    check(three != tl::expected<int, Error>(4) && tl::unexpected<Error>(Error::odd) == odd &&
              tl::unexpected<Error>(Error::odd) != three,
          "compare in either order");
}

static void exceptions() {
    bool thrown = false;
    try {
        (void)parse("x").value();
    } catch (const tl::bad_expected_access<Error> &e) {
        thrown = e.error() == Error::not_a_digit && e.what() && std::strlen(e.what()) > 0;
    }
    check(thrown, "value() on an error throws bad_expected_access with the error");
}

constexpr tl::expected<int, int> constant(21);
constexpr tl::expected<int, int> constant_error(tl::unexpect, 5);
static_assert(constant.has_value() && *constant == 21, "a value in a constant expression");
static_assert(!constant_error && constant_error.error() == 5, "an error in a constant expression");

int main() {
    values_and_errors();
    monadic_chain();
    void_results();
    payloads_and_states();
    comparisons();
    exceptions();
    std::printf("tl-expected (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
