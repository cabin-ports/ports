// Consumer test for cabin-ports/bext-mp: meta-programs through the public API,
// checked at compile time and by running the same meta-functions at run time.
#include <mp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <type_traits>

template <class...> struct type_list {};

namespace {

int failures = 0;

void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

template <std::size_t N, class... Ts>
using at_c = mp::type_of<std::array{mp::meta<Ts>...}[N]>;

static_assert(std::is_same_v<int, at_c<0, int, bool, float>>);
static_assert(std::is_same_v<bool, at_c<1, int, bool, float>>);
static_assert(std::is_same_v<float, at_c<2, int, bool, float>>);
static_assert(mp::meta<int> == mp::meta<int>);
static_assert(mp::meta<int> != mp::meta<void>);
static_assert(mp::meta<int> != mp::meta<const int>);
static_assert(std::is_same_v<type_list<int, bool>,
                             mp::apply_t<type_list, std::array{mp::meta<int>, mp::meta<bool>}>>);
static_assert(!mp::invoke<std::is_const>(mp::meta<int>));
static_assert(mp::invoke<std::is_const>(mp::meta<const int>));
static_assert(std::is_same_v<const int, mp::type_of<mp::invoke<std::add_const>(mp::meta<int>)>>);

} // namespace

int main() {
    int n = 0;
    mp::unroll<3>([&] { ++n; });
    check(n == 3, "unroll<3> runs the function three times");

    int index_sum = 0;
    mp::unroll<4>([&]<auto I> { index_sum += static_cast<int>(I) + 1; });
    check(index_sum == 1 + 2 + 3 + 4, "unroll passes indices 0..3");

    constexpr auto v = mp::vector{mp::meta<int>, mp::meta<bool>, mp::meta<double>};
    std::size_t total = 0;
    int visited = 0;
    mp::for_each<v>([&]<mp::info m> {
        total += sizeof(mp::type_of<m>);
        ++visited;
    });
    check(visited == 3, "for_each visits every element");
    check(total == sizeof(int) + sizeof(bool) + sizeof(double), "for_each exposes each element's type");

    // The same meta-functions run at run time on runtime info values.
    check(v.size() == 3, "vector size");
    check(v[1] == mp::meta<bool>, "vector element");
    auto is_int = []<mp::info m> { return m == mp::meta<int>; };
    mp::info runtime_info = v[0];
    check(mp::invoke(is_int, runtime_info), "invoke dispatches a runtime info to its compile-time value");
    runtime_info = v[1];
    check(!mp::invoke(is_int, runtime_info), "invoke on a different runtime info");
    check(mp::invoke<std::is_const>(mp::meta<const int>), "invoke<is_const> at run time");
    check(!mp::invoke<std::is_const>(mp::meta<int>), "invoke<is_const> negative at run time");

    if (failures == 0) std::puts("bext-mp ok");
    return failures == 0 ? 0 : 1;
}
