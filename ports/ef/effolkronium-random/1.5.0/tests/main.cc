// Drives the static, thread-local and per-object front ends, and one custom
// engine, with fixed seeds: the engine's standardized output and state, get()
// for numbers, characters, containers, weights and distributions, and shuffling.
// Distribution outputs are implementation-defined, so only ranges,
// reproducibility under one seed, and wide frequency bounds are checked.
#include <effolkronium/random.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <list>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using Random = effolkronium::random_static;
using Local = effolkronium::random_local;

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// [rand.predef]: the 10000th output of a default-constructed std::mt19937.
const std::mt19937::result_type mt19937_10000th = 4123659995u;

void test_engine()
{
    Random::seed();
    check(Random::is_equal(std::mt19937{}), "seed() restores the default seed");
    Random::discard(9999);
    check(Random::get() == mt19937_10000th, "the 10000th output is the standard's");
    check((Random::min)() == 0u && (Random::max)() == 0xffffffffu, "engine range");

    std::seed_seq seq{1u, 2u, 3u};
    Random::seed(seq);
    std::seed_seq same{1u, 2u, 3u};
    check(Random::is_equal(std::mt19937(same)), "seed(seed_seq&) seeds the engine");

    std::mt19937 copy = Random::get_engine();
    Random::engine().discard(1);
    check(!Random::is_equal(copy), "engine() is the internal engine, get_engine() a copy");

    std::stringstream state;
    Random::serialize(state);
    std::vector<int> first;
    std::vector<int> replay;
    for (int i = 0; i < 16; ++i) {
        first.push_back(Random::get(0, 1000000));
    }
    Random::deserialize(state);
    for (int i = 0; i < 16; ++i) {
        replay.push_back(Random::get(0, 1000000));
    }
    check(first == replay, "deserialize() replays the serialized state");

    Random::seed();
    Random::reseed();
    check(!Random::is_equal(std::mt19937{}), "reseed() reseeds from the default seeder");
}

void test_ranges()
{
    Random::seed(42u);
    bool in_range = true;
    bool low = false;
    bool high = false;
    bool reversed = true;
    bool real = true;
    bool letters = true;
    bool bytes = true;
    for (int i = 0; i < 2000; ++i) {
        const int v = Random::get(-5, 5);
        in_range = in_range && v >= -5 && v <= 5;
        low = low || v == -5;
        high = high || v == 5;
        const int r = Random::get(5, -5);
        reversed = reversed && r >= -5 && r <= 5;
        const double d = Random::get(1.0, 2.0);
        const float f = Random::get(2.5f, -2.5f);
        real = real && d >= 1.0 && d <= 2.0 && f >= -2.5f && f <= 2.5f;
        const char c = Random::get('a', 'z');
        letters = letters && c >= 'a' && c <= 'z';
        const unsigned char u = Random::get<unsigned char>(250, 255);
        const signed char s = Random::get<signed char>(-3, 3);
        bytes = bytes && u >= 250 && s >= -3 && s <= 3;
    }
    check(in_range && low && high, "get(-5, 5) stays in range and reaches both ends");
    check(reversed, "get(5, -5) accepts a reversed range");
    check(real, "real ranges, also reversed");
    check(letters, "character range");
    check(bytes, "unsigned and signed byte ranges");

    static_assert(std::is_same<decltype(Random::get(2.5f, 1.f)), float>::value, "float in, float out");
    static_assert(std::is_same<decltype(Random::get('a', 'b')), char>::value, "char in, char out");
    static_assert(std::is_same<decltype(Random::get<Random::common>(1, 2.5)), double>::value,
                  "common picks the common type");
    const double mixed = Random::get<Random::common>(1, 2.5);
    check(mixed >= 1.0 && mixed <= 2.5, "common range of int and double");

    bool never = false;
    bool always = true;
    int heads = 0;
    for (int i = 0; i < 2000; ++i) {
        never = never || Random::get<bool>(0.0);
        always = always && Random::get<bool>(1.0);
        heads += Random::get<bool>() ? 1 : 0;
    }
    check(!never && always, "get<bool>(0) is never and get<bool>(1) always true");
    check(heads > 800 && heads < 1200, "get<bool>() is a fair coin");

    const int successes = Random::get<std::binomial_distribution<>>(10, 0.5);
    check(successes >= 0 && successes <= 10, "get<Dist>(args...) builds the distribution");
    std::uniform_int_distribution<int> seven(7, 7);
    check(Random::get(seven) == 7, "get(dist) draws from a caller's distribution");

    std::vector<int> once;
    std::vector<int> twice;
    Random::seed(99u);
    for (int i = 0; i < 32; ++i) {
        once.push_back(Random::get(0, 1000000));
    }
    Random::seed(99u);
    for (int i = 0; i < 32; ++i) {
        twice.push_back(Random::get(0, 1000000));
    }
    check(once == twice, "one seed, one sequence");
}

void test_containers()
{
    Random::seed(3u);
    std::vector<int> v{0, 1, 2};
    int array[3] = {0, 1, 2};
    bool listed_seen[3] = {false, false, false};
    bool container_seen[3] = {false, false, false};
    bool array_seen[3] = {false, false, false};
    bool members = true;
    for (int i = 0; i < 300; ++i) {
        const int pick = Random::get({0, 1, 2});
        const std::vector<int>::iterator it = Random::get(v);
        const int *p = Random::get(array);
        members = members && pick >= 0 && pick <= 2 && it != v.end() && p >= array && p < array + 3;
        if (members) {
            listed_seen[pick] = true;
            container_seen[*it] = true;
            array_seen[*p] = true;
        }
    }
    bool every = members;
    for (int i = 0; i < 3; ++i) {
        every = every && listed_seen[i] && container_seen[i] && array_seen[i];
    }
    check(every, "get(initializer_list), get(container) and get(array) pick every element");
    std::vector<int> empty;
    check(Random::get(empty) == empty.end(), "get(empty container) is end()");
    check(Random::get(v.begin() + 1, v.begin() + 2) == v.begin() + 1, "get(first, last) of one element");

    const std::vector<int> dice = Random::get<std::vector>(1, 6, 50);
    bool dice_ok = dice.size() == 50;
    for (std::size_t i = 0; i < dice.size(); ++i) {
        dice_ok = dice_ok && dice[i] >= 1 && dice[i] <= 6;
    }
    check(dice_ok, "get<std::vector>(from, to, size)");
    const std::list<int> listed = Random::get<std::list>(1, 6, 10);
    check(listed.size() == 10 && *std::min_element(listed.begin(), listed.end()) >= 1 &&
              *std::max_element(listed.begin(), listed.end()) <= 6,
          "get<std::list>(from, to, size), a container without reserve()");
    const std::array<char, 5> vowels = Random::get<std::array, 5>('a', 'e');
    check(*std::min_element(vowels.begin(), vowels.end()) >= 'a' &&
              *std::max_element(vowels.begin(), vowels.end()) <= 'e',
          "get<std::array, N>(from, to)");
    const auto halves = Random::get<std::vector, Random::common>(1, 2.0, 8);
    static_assert(std::is_same<decltype(halves), const std::vector<double>>::value,
                  "a common-type container holds the common type");
    check(halves.size() == 8 && *std::min_element(halves.begin(), halves.end()) >= 1.0 &&
              *std::max_element(halves.begin(), halves.end()) <= 2.0,
          "get<std::vector, common>(from, to, size)");
}

void test_weights()
{
    Random::seed(5u);
    // The zero weight comes first, where an off-by-one would pick it.
    const std::map<std::string, unsigned> integral{{"a-never", 0u}, {"b-always", 3u}};
    const std::map<char, double> real{{'a', 0.0}, {'b', 2.5}};
    bool zero_weight_skipped = true;
    for (int i = 0; i < 500; ++i) {
        const std::map<std::string, unsigned>::const_iterator s = Random::get<Random::weight>(integral);
        const std::map<char, double>::const_iterator c = Random::get<Random::weight>(real);
        zero_weight_skipped = zero_weight_skipped && s != integral.end() && s->first == "b-always" &&
                              c != real.end() && c->first == 'b';
    }
    check(zero_weight_skipped, "weighted choice never picks a zero weight");

    const std::map<int, unsigned> none{{1, 0u}, {2, 0u}};
    const std::map<int, double> empty;
    check(Random::get<Random::weight>(none) == none.end() && Random::get<Random::weight>(empty) == empty.end(),
          "zero total weight yields end()");

    const std::map<char, unsigned> odds{{'x', 1u}, {'y', 3u}};
    const std::map<char, double> real_odds{{'x', 0.5}, {'y', 1.5}};
    int ys = 0;
    int real_ys = 0;
    for (int i = 0; i < 4000; ++i) {
        ys += Random::get<Random::weight>(odds)->first == 'y' ? 1 : 0;
        real_ys += Random::get<Random::weight>(real_odds)->first == 'y' ? 1 : 0;
    }
    check(ys > 2700 && ys < 3300, "unsigned weights 1:3 pick the heavier key about 3 times in 4");
    check(real_ys > 2700 && real_ys < 3300, "double weights 1:3 pick the heavier key about 3 times in 4");
}

void test_shuffle()
{
    std::vector<int> base(20);
    std::iota(base.begin(), base.end(), 0);
    std::vector<int> a = base;
    std::vector<int> b = base;
    Random::seed(7u);
    Random::shuffle(a);
    Random::seed(7u);
    Random::shuffle(b.begin(), b.end());
    check(a == b && a != base, "one seed, one permutation");
    std::sort(a.begin(), a.end());
    check(a == base, "shuffle permutes");
}

// Each thread owns its engine, seeded by the default seeder on first use.
void test_thread_local()
{
    using TL = effolkronium::random_thread_local;
    TL::seed();
    bool other_ok = false;
    std::thread other([&other_ok] {
        const bool own_engine = !TL::is_equal(std::mt19937{});
        TL::seed();
        TL::discard(9999);
        other_ok = own_engine && TL::get() == mt19937_10000th;
    });
    other.join();
    check(other_ok, "random_thread_local: another thread has its own engine");
    check(TL::is_equal(std::mt19937{}), "random_thread_local: this thread's engine is untouched");
}

// random_local re-implements the drawing members for its per-object engine.
void test_local()
{
    Local a;
    Local b;
    a.seed();
    b.seed();
    a.discard(9999);
    check(a.get() == mt19937_10000th && b.is_equal(std::mt19937{}), "random_local: each object owns its engine");

    b.seed(11u);
    bool ranges = true;
    for (int i = 0; i < 500; ++i) {
        const int v = b.get(5, -5);
        const double d = b.get(1.0, 2.0);
        const char c = b.get('a', 'z');
        const double m = b.get<Local::common>(1, 2.5);
        ranges = ranges && v >= -5 && v <= 5 && d >= 1.0 && d <= 2.0 && c >= 'a' && c <= 'z' && m >= 1.0 &&
                 m <= 2.5 && b.get<bool>(1.0) && !b.get<bool>(0.0);
    }
    check(ranges, "random_local: ranges");
    const std::vector<int> dice = b.get<std::vector>(1, 6, 20);
    check(dice.size() == 20 && *std::min_element(dice.begin(), dice.end()) >= 1 &&
              *std::max_element(dice.begin(), dice.end()) <= 6,
          "random_local: get<std::vector>(from, to, size)");
    const std::map<std::string, unsigned> weights{{"a-never", 0u}, {"b-always", 3u}};
    const std::map<char, double> real_odds{{'x', 0.5}, {'y', 1.5}};
    bool zero_weight_skipped = true;
    int real_ys = 0;
    for (int i = 0; i < 4000; ++i) {
        zero_weight_skipped = zero_weight_skipped && b.get<Local::weight>(weights)->first == "b-always";
        real_ys += b.get<Local::weight>(real_odds)->first == 'y' ? 1 : 0;
    }
    check(zero_weight_skipped && real_ys > 2700 && real_ys < 3300, "random_local: weighted choice");

    std::vector<int> x(20);
    std::iota(x.begin(), x.end(), 0);
    std::vector<int> y = x;
    a.seed(7u);
    b.seed(7u);
    a.shuffle(x);
    b.shuffle(y);
    check(x == y, "random_local: one seed, one permutation");

    std::stringstream state;
    a.serialize(state);
    const unsigned next = static_cast<unsigned>(a.get());
    b.deserialize(state);
    check(static_cast<unsigned>(b.get()) == next && b.is_equal(a.get_engine()),
          "random_local: one object's state restores another");
}

// The front ends are templates over the engine; [rand.predef] fixes
// std::mt19937_64's 10000th output too.
void test_custom_engine()
{
    effolkronium::basic_random_local<std::mt19937_64> wide;
    wide.seed();
    wide.discard(9999);
    check(wide.get() == 9981545732273789042ull, "basic_random_local<std::mt19937_64>: the 10000th output");
    const unsigned long long big = wide.get<unsigned long long>(1ull << 40, 1ull << 41);
    check(big >= 1ull << 40 && big <= 1ull << 41, "basic_random_local<std::mt19937_64>: a 64-bit range");
}

} // namespace

int main()
{
    test_engine();
    test_ranges();
    test_containers();
    test_weights();
    test_shuffle();
    test_thread_local();
    test_local();
    test_custom_engine();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("effolkronium-random 1.5.0 ok");
    return 0;
}
