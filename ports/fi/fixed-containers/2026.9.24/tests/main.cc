// C++ consumer: the README.md examples (FixedVector :66-79, FixedCircularQueue :165-181,
// FixedBitset :183-196, FixedString :198-212, FixedMap :214-243, FixedUnorderedMap :245-274,
// EnumMap :276-298) as compile-time checks, then the same operations at run time.
#include <fixed_containers/enum_map.hpp>
#include <fixed_containers/fixed_bitset.hpp>
#include <fixed_containers/fixed_circular_queue.hpp>
#include <fixed_containers/fixed_map.hpp>
#include <fixed_containers/fixed_string.hpp>
#include <fixed_containers/fixed_unordered_map.hpp>
#include <fixed_containers/fixed_vector.hpp>

#include <cstdio>

using namespace fixed_containers;

enum class Color
{
    RED,
    YELLOW,
    BLUE
};

namespace
{
constexpr auto v1 = []()
{
    FixedVector<int, 11> v{};
    v.push_back(0);
    v.emplace_back(1);
    v.push_back(2);
    return v;
}();
static_assert(v1[0] == 0);
static_assert(v1[1] == 1);
static_assert(v1[2] == 2);
static_assert(v1.size() == 3);
static_assert(v1.capacity() == 11);

constexpr auto q1 = []()
{
    FixedCircularQueue<int, 3> q{};
    q.push(55);
    q.push(66);
    q.push(77);
    q.push(88);
    q.push(99);
    return q;
}();
static_assert(q1.front() == 77);
static_assert(q1.back() == 99);
static_assert(q1.size() == 3);

constexpr auto b1 = []()
{
    FixedBitset<4> b{"0101"};
    b.flip(0);
    return b;
}();
static_assert(b1.test(0) == 0);
static_assert(b1.test(1) == 0);
static_assert(b1.test(2) == 1);
static_assert(b1.test(3) == 0);

constexpr auto s1 = []()
{
    FixedString<11> s{"012"};
    s.at(1) = 'b';
    return s;
}();
static_assert(s1.at(0) == '0');
static_assert(s1.at(1) == 'b');
static_assert(s1.at(2) == '2');
static_assert(s1.size() == 3);

constexpr auto m1 = []()
{
    FixedMap<int, int, 11> m{};
    m.insert({2, 20});
    m[4] = 40;
    return m;
}();
static_assert(!m1.contains(1));
static_assert(m1.contains(2));
static_assert(m1.at(4) == 40);
static_assert(m1.size() == 2);
static_assert(m1.max_size() == 11);

constexpr auto u1 = []()
{
    FixedUnorderedMap<int, int, 11> m{};
    m.insert({2, 20});
    m[4] = 40;
    return m;
}();
static_assert(!u1.contains(1));
static_assert(u1.contains(2));
static_assert(u1.at(4) == 40);
static_assert(u1.size() == 2);
static_assert(u1.max_size() == 11);

constexpr auto e1 = []()
{
    EnumMap<Color, int> m{};
    m.insert({Color::RED, 20});
    m[Color::YELLOW] = 40;
    return m;
}();
static_assert(!e1.contains(Color::BLUE));
static_assert(e1.contains(Color::RED));
static_assert(e1.at(Color::YELLOW) == 40);
static_assert(e1.size() == 2);

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}
}  // namespace

int main()
{
    // Run-time: erase and insert on a vector.
    FixedVector<int, 5> v{1, 2, 3, 4};
    v.erase(v.begin() + 1);
    v.insert(v.begin() + 2, 9);
    check(v.size() == 4 && v[0] == 1 && v[1] == 3 && v[2] == 9 && v[3] == 4, "vector erase/insert");
    v.push_back(7);
    check(v.size() == v.capacity(), "vector fills to capacity");

    FixedCircularQueue<int, 3> q{};
    for (int i = 1; i <= 5; ++i)
    {
        q.push(i);
    }
    check(q.size() == 3 && q.front() == 3 && q.back() == 5, "circular queue overwrites oldest");

    FixedBitset<4> b{"0101"};
    b.flip(0);
    check(!b.test(0) && !b.test(1) && b.test(2) && !b.test(3), "bitset flip");

    FixedString<11> s{"012"};
    s.at(1) = 'b';
    check(s.size() == 3 && s.at(0) == '0' && s.at(1) == 'b' && s.at(2) == '2', "string at");

    FixedMap<int, int, 11> m{};
    m.insert({2, 20});
    m[4] = 40;
    check(!m.contains(1) && m.contains(2) && m.at(4) == 40 && m.size() == 2, "map insert");
    check(m.erase(2) == 1 && !m.contains(2) && m.size() == 1, "map erase");

    FixedUnorderedMap<int, int, 11> u{};
    for (int i = 0; i < 11; ++i)
    {
        u[i * 7] = i;
    }
    bool all_found = u.size() == 11;
    for (int i = 0; i < 11; ++i)
    {
        all_found = all_found && u.contains(i * 7) && u.at(i * 7) == i;
    }
    check(all_found && !u.contains(1), "unordered map holds 11 keys");

    EnumMap<Color, int> e{};
    e.insert({Color::RED, 20});
    e[Color::YELLOW] = 40;
    check(!e.contains(Color::BLUE) && e.contains(Color::RED) && e.at(Color::YELLOW) == 40 &&
              e.size() == 2,
          "enum map insert");

    return failures;
}
