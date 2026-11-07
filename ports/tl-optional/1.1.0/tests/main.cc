// The C++ consumer: tl::optional in C++11 - values and tl::nullopt,
// value_or and bad_optional_access, the map/and_then/or_else/map_or/
// map_or_else chain (including member pointers and a void map),
// conjunction, disjunction and take, optional references that rebind on
// assignment, emplace, reset and swap with their destructors, move-only
// and in-place payloads, comparisons, and std::hash.
#include <tl/optional.hpp>

#include <cstdio>
#include <cstring>
#include <functional>
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

// A non-negative decimal number of at most a million, if the text is one.
static tl::optional<int> parse(const std::string &text) {
    if (text.empty()) return tl::nullopt;
    long value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return tl::nullopt;
        value = value * 10 + (c - '0');
        if (value > 1000000) return {};
    }
    return static_cast<int>(value);
}

static tl::optional<int> halve(int n) {
    if (n % 2 != 0) return tl::nullopt;
    return n / 2;
}

struct Word {
    std::string text;
    std::size_t length() const { return text.size(); }
};

static void values_and_empties() {
    const tl::optional<int> answer = parse("42");
    check(answer.has_value() && static_cast<bool>(answer) && *answer == 42 && answer.value() == 42,
          "a parsed number is the value");
    check(!parse("") && !parse("4x2") && !parse("99999999"), "bad input is empty");
    check(parse("x").value_or(-1) == -1 && answer.value_or(-1) == 42, "value_or falls back only when empty");

    bool thrown = false;
    try {
        (void)parse("x").value();
    } catch (const tl::bad_optional_access &e) {
        thrown = e.what() && std::strlen(e.what()) > 0;
    }
    check(thrown, "value() on an empty optional throws bad_optional_access");
}

static void monadic_chain() {
    int mapped = 0;
    const auto shout = [&mapped](int n) {
        ++mapped;
        return std::to_string(n) + "!";
    };
    const tl::optional<std::string> even = parse("84").and_then(halve).map(shout);
    check(even && *even == "42!" && mapped == 1, "and_then and map run on a value");
    const tl::optional<std::string> odd = parse("85").and_then(halve).map(shout);
    check(!odd && mapped == 1, "an empty result skips map");

    int chained = 0;
    const auto count_halve = [&chained](int n) {
        ++chained;
        return halve(n);
    };
    check(!parse("x").and_then(count_halve) && !parse("x").map(shout) && chained == 0 && mapped == 1,
          "an empty input skips and_then and map");
    check(parse("8").and_then(count_halve) == 4 && chained == 1, "and_then runs once on a value");

    const tl::optional<Word> word(Word{"cabin"});
    const tl::optional<std::size_t> length = word.map(&Word::length);
    check(length && *length == 5, "map takes a member function pointer");

    int recovered = 0;
    const auto recover = [&recovered]() -> tl::optional<int> {
        ++recovered;
        return 0;
    };
    check(parse("").or_else(recover).value() == 0 && recovered == 1, "or_else runs when empty");
    check(parse("5").or_else(recover).value() == 5 && recovered == 1, "or_else leaves a value alone");
    int noticed = 0;
    const auto notice = [&noticed] { ++noticed; };
    check(!parse("").or_else(notice) && noticed == 1 && parse("7").or_else(notice) == 7 && noticed == 1,
          "or_else with a void callback runs only when empty");

    const auto twice = [](int n) { return 2 * n; };
    int fallbacks = 0;
    const auto fallback = [&fallbacks] {
        ++fallbacks;
        return -2;
    };
    check(parse("4").map_or(twice, -1) == 8 && parse("").map_or(twice, -1) == -1, "map_or");
    check(parse("4").map_or_else(twice, fallback) == 8 && fallbacks == 0, "map_or_else maps a value");
    check(parse("").map_or_else(twice, fallback) == -2 && fallbacks == 1, "map_or_else falls back when empty");

    int seen = 0;
    const tl::optional<tl::monostate> visited = parse("9").map([&seen](int n) { seen = n; });
    check(visited.has_value() && seen == 9, "a void map gives an engaged optional<monostate>");
    check(!parse("").map([&seen](int n) { seen = n; }) && seen == 9, "a void map on empty does nothing");
}

static void combinators() {
    check(tl::make_optional(42).conjunction(13) == 13, "conjunction takes the argument after a value");
    check(!tl::optional<int>().conjunction(13), "conjunction stays empty");
    check(tl::make_optional(42).disjunction(13) == 42, "disjunction keeps a value");
    check(tl::optional<int>().disjunction(13) == 13, "disjunction falls back when empty");

    tl::optional<std::unique_ptr<int>> box(std::unique_ptr<int>(new int(6)));
    const tl::optional<std::unique_ptr<int>> taken = box.take();
    check(taken && **taken == 6 && !box, "take moves the value out and empties the source");
}

static void references() {
    int i = 42, j = 8;
    tl::optional<int &> ref = i;
    check(ref && *ref == 42 && &*ref == &i, "a reference refers to its object");
    i = 12;
    check(*ref == 12, "a reference sees changes");
    *ref = 13;
    check(i == 13, "writing through a reference changes the object");
    ref = j;
    check(&*ref == &j && i == 13 && j == 8, "assignment rebinds instead of assigning through");
    const tl::optional<int> copy = ref.map([](int &n) { return n + 1; });
    check(copy && *copy == 9, "map on a reference");
    ref = tl::nullopt;
    check(!ref && j == 8, "nullopt empties a reference");
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
    tl::optional<std::unique_ptr<int>> owned(std::unique_ptr<int>(new int(5)));
    tl::optional<std::unique_ptr<int>> moved(std::move(owned));
    check(moved && **moved == 5, "a move-only value moves");

    const tl::optional<std::vector<int>> list(tl::in_place, {1, 2, 3});
    check(list && list->size() == 3 && (*list)[2] == 3, "in-place construction from a list");
    check(tl::make_optional<std::string>(3, 'z') == std::string("zzz"), "make_optional constructs in place");

    {
        tl::optional<Tracked> slot(tl::in_place, 1);
        check(alive == 1, "the value is constructed once");
        slot.reset();
        check(alive == 0 && !slot, "reset destroys the value");
        slot.emplace(2);
        check(alive == 1 && slot->id == 2, "emplace constructs a value");
        tl::optional<Tracked> other;
        slot.swap(other);
        check(!slot && other && other->id == 2 && alive == 1, "swap moves the value across");
        other.emplace(3);
        check(other->id == 3 && alive == 1, "emplace replaces the value");
        slot = other;
        check(slot && slot->id == 3 && alive == 2, "copy assignment copies the value");
        slot = tl::nullopt;
        check(!slot && alive == 1, "assigning nullopt destroys the value");
    }
    check(alive == 0, "every value is destroyed");

    {
        tl::optional<Tracked> a(tl::in_place, 4), b(tl::in_place, 5);
        swap(a, b);
        check(a && b && a->id == 5 && b->id == 4 && alive == 2, "swap exchanges two values");
        a = b;
        check(a->id == 4 && b->id == 4 && alive == 2, "copy assignment replaces a value");
    }
    check(alive == 0, "every swapped value is destroyed");
}

static void comparisons() {
    const tl::optional<int> three(3), none;
    check(three == 3 && three != 4 && !(three == 4) && !(three != 3) && !(none == 3),
          "compare with a value");
    check(3 == three && 4 != three && !(4 == three) && !(3 != three) && !(3 == none),
          "compare a value with an optional");
    check(none == tl::nullopt && tl::nullopt == none && three != tl::nullopt && tl::nullopt != three &&
              !(three == tl::nullopt) && !(tl::nullopt == three) && !(none != tl::nullopt) &&
              !(tl::nullopt != none),
          "compare with nullopt in either order");
    check(three == tl::make_optional(3) && none == tl::optional<int>() && three != none &&
              three != tl::make_optional(4) && !(three == tl::make_optional(4)) && !(three == none) &&
              !(three != tl::make_optional(3)),
          "compare two optionals");
    check(none < three && !(three < none) && three < tl::make_optional(4) && three >= halve(6) &&
              !(three >= tl::make_optional(4)) && tl::nullopt < three && !(three < tl::nullopt),
          "an empty optional orders first");
    check(std::hash<tl::optional<int>>()(three) == std::hash<int>()(3), "hash of a value");
}

int main() {
    values_and_empties();
    monadic_chain();
    combinators();
    references();
    payloads_and_states();
    comparisons();
    std::printf("tl-optional (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
