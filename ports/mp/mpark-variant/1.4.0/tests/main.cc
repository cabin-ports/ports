// Exercises mpark::variant as a C++11 consumer against the std::variant semantics it implements: active
// alternative and access, visitation, assignment and emplace with lifetimes counted, the valueless state
// after a throwing move assignment, swap, ordering, and hashing.
#include <mpark/variant.hpp>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

// Counts live objects so leaks and double destruction show up as a nonzero balance.
struct Counted {
    static int live;
    int value;
    explicit Counted(int v) : value(v) { ++live; }
    Counted(const Counted &other) : value(other.value) { ++live; }
    Counted(Counted &&other) noexcept : value(other.value) {
        other.value = -1;
        ++live;
    }
    Counted &operator=(const Counted &) = default;
    Counted &operator=(Counted &&) = default;
    ~Counted() { --live; }
};
int Counted::live = 0;

struct ThrowsOnMove {
    ThrowsOnMove() {}
    ThrowsOnMove(ThrowsOnMove &&) { throw std::runtime_error("no"); }
    ThrowsOnMove &operator=(ThrowsOnMove &&) { throw std::runtime_error("no"); }
};

struct Describe {
    std::string operator()(int i) const { return "int " + std::to_string(i); }
    std::string operator()(const std::string &s) const { return "string " + s; }
    std::string operator()(const Counted &c) const { return "counted " + std::to_string(c.value); }
};

struct Pair {
    int operator()(int a, int b) const { return 10 * a + b; }
    int operator()(int a, const std::string &b) const { return 100 * a + static_cast<int>(b.size()); }
    int operator()(const std::string &a, int b) const { return -(static_cast<int>(a.size()) * 10 + b); }
    int operator()(const std::string &, const std::string &) const { return 0; }
};

struct IntOrThrows {
    int operator()(int) const { return 0; }
    int operator()(const ThrowsOnMove &) const { return 1; }
};

using V = mpark::variant<int, std::string, Counted>;

void testAccess() {
    static_assert(mpark::variant_size<V>::value == 3, "three alternatives");
    static_assert(std::is_same<mpark::variant_alternative_t<1, V>, std::string>::value, "index 1 is std::string");

    V v;  // Value-initializes the first alternative.
    CHECK(v.index() == 0 && mpark::get<int>(v) == 0 && !v.valueless_by_exception());

    v = 42;
    CHECK(v.index() == 0 && mpark::holds_alternative<int>(v) && mpark::get<0>(v) == 42);
    CHECK(mpark::get_if<std::string>(&v) == nullptr && *mpark::get_if<int>(&v) == 42);

    v = std::string("hi");
    CHECK(v.index() == 1 && mpark::get<std::string>(v) == "hi" && mpark::get_if<0>(&v) == nullptr);
    bool threw = false;
    try {
        (void)mpark::get<int>(v);
    } catch (const mpark::bad_variant_access &) {
        threw = true;
    }
    CHECK(threw);

    // Converting construction picks the best match: a string literal is not an int.
    V fromLiteral("text");
    CHECK(fromLiteral.index() == 1 && mpark::get<1>(fromLiteral) == "text");

    V inPlace(mpark::in_place_index_t<1>(), std::size_t{3}, 'x');
    CHECK(mpark::get<std::string>(inPlace) == "xxx");
    V inPlaceType(mpark::in_place_type_t<Counted>(), 7);
    CHECK(inPlaceType.index() == 2 && mpark::get<Counted>(inPlaceType).value == 7);

    mpark::variant<mpark::monostate, int> m;
    CHECK(m.index() == 0 && mpark::holds_alternative<mpark::monostate>(m));
}

void testVisit() {
    V v = 5;
    CHECK(mpark::visit(Describe(), v) == "int 5");
    v = std::string("abc");
    CHECK(mpark::visit(Describe(), v) == "string abc");
    v.emplace<Counted>(9);
    CHECK(mpark::visit(Describe(), v) == "counted 9");

    // Visiting two variants dispatches on both active alternatives.
    mpark::variant<int, std::string> a = 3, b = 4;
    CHECK(mpark::visit(Pair(), a, b) == 34);
    b = std::string("four");
    CHECK(mpark::visit(Pair(), a, b) == 304);
    a = std::string("ab");
    b = 7;
    CHECK(mpark::visit(Pair(), a, b) == -27);
}

void testLifetimes() {
    {
        V v(mpark::in_place_type_t<Counted>(), 1);
        CHECK(Counted::live == 1);
        v = std::string("replaced");  // Destroys the Counted.
        CHECK(Counted::live == 0);
        v.emplace<2>(2);
        CHECK(Counted::live == 1 && mpark::get<2>(v).value == 2);

        V copy = v;
        CHECK(Counted::live == 2 && mpark::get<Counted>(copy).value == 2);
        V moved = std::move(copy);
        CHECK(Counted::live == 3 && mpark::get<Counted>(moved).value == 2);
        CHECK(mpark::get<Counted>(copy).value == -1);  // Moved-from, but still holding a Counted.

        std::vector<V> many(4, V(mpark::in_place_type_t<Counted>(), 5));
        CHECK(Counted::live == 7);
        many.clear();
        CHECK(Counted::live == 3);
    }
    CHECK(Counted::live == 0);
}

// Move assignment from another alternative whose move constructor throws leaves the variant valueless
// ([variant.assign]: "the variant will hold no value"); a throwing emplace only might.
void testValueless() {
    using T = mpark::variant<int, ThrowsOnMove>;
    T v = 1;
    T source(mpark::in_place_type_t<ThrowsOnMove>{});
    bool threw = false;
    try {
        v = std::move(source);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw && v.valueless_by_exception() && v.index() == mpark::variant_npos);
    threw = false;
    try {
        (void)mpark::visit(IntOrThrows(), v);
    } catch (const mpark::bad_variant_access &) {
        threw = true;
    }
    CHECK(threw);
    v = 3;  // Assignment recovers a value.
    CHECK(!v.valueless_by_exception() && mpark::get<int>(v) == 3);
}

void testSwapCompareHash() {
    using IS = mpark::variant<int, std::string>;
    IS a = 1, b = std::string("z");
    a.swap(b);
    CHECK(mpark::get<std::string>(a) == "z" && mpark::get<int>(b) == 1);
    using std::swap;
    swap(a, b);
    CHECK(mpark::get<int>(a) == 1 && mpark::get<std::string>(b) == "z");
    IS c = 5;
    a.swap(c);  // Same alternative: the values are swapped.
    CHECK(mpark::get<int>(a) == 5 && mpark::get<int>(c) == 1);

    // Ordering compares the index first, then the values of equal alternatives.
    IS small = 100, large = std::string("a"), other = 7;
    CHECK(small < large && large > small && !(large < small) && !(small > large));
    CHECK(other < small && other <= small && small >= other && other != small);
    CHECK(small == IS(100) && !(small == large) && small != large);

    std::hash<IS> hasher;
    CHECK(hasher(small) == hasher(IS(100)));
    CHECK(std::hash<mpark::monostate>()(mpark::monostate()) == std::hash<mpark::monostate>()(mpark::monostate()));
}

}  // namespace

int main() {
    testAccess();
    testVisit();
    testLifetimes();
    testValueless();
    testSwapCompareHash();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mpark-variant: ok\n");
    return 0;
}
