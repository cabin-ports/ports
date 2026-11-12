// Views a C array, a std::array, and a std::vector through tcb::span:
// static and dynamic extents, the extents of first, last and subspan,
// element access and iteration, writes through a span, conversion to a
// span of const, as_bytes and as_writable_bytes, make_span, tcb::get, and
// std::tuple_size.  The standard is C++11 except on MSVC (C++14), where
// upstream's default contract checks, which call std::terminate, are
// compiled unless NDEBUG is defined; no call here violates one.
#include <tcb/span.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <type_traits>
#include <vector>

namespace {

constexpr int primes[] = {2, 3, 5, 7, 11};
constexpr tcb::span<const int, 5> prime_view(primes);
static_assert(prime_view.size() == 5 && prime_view.size_bytes() == 5 * sizeof(int),
              "a constexpr span over a constexpr array");

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "tcb-span test failed: %s\n", what);
        ++failures;
    }
}

int sum(tcb::span<const int> values) {
    return std::accumulate(values.begin(), values.end(), 0);
}

}  // namespace

int main() {
    static_assert(tcb::dynamic_extent == static_cast<std::size_t>(-1), "dynamic_extent");
    static_assert(tcb::span<int>::extent == tcb::dynamic_extent, "a dynamic default");

    int raw[] = {1, 2, 3, 4, 5, 6};
    tcb::span<int, 6> fixed(raw);
    auto head = fixed.first<2>();
    auto middle = fixed.subspan<1, 3>();
    auto rest = fixed.subspan<2>();
    static_assert(decltype(head)::extent == 2 && decltype(middle)::extent == 3 &&
                      decltype(rest)::extent == 4,
                  "static subviews of a static span");
    check(head[1] == 2 && middle.front() == 2 && middle.back() == 4 && rest[0] == 3 &&
              fixed.last<1>()[0] == 6,
          "static subviews");

    // Writing through a span writes the array it views.
    for (int& value : middle)
        value *= 10;
    check(raw[0] == 1 && raw[1] == 20 && raw[3] == 40 && raw[4] == 5, "writes through a span");

    std::vector<int> numbers(10);
    std::iota(numbers.begin(), numbers.end(), 1);
    tcb::span<int> dynamic(numbers);
    auto tail = dynamic.last(3);
    auto window = dynamic.subspan(4, 2);
    auto suffix = dynamic.subspan(7);
    static_assert(decltype(tail)::extent == tcb::dynamic_extent &&
                      decltype(dynamic.first<4>())::extent == 4,
                  "dynamic subviews");
    check(dynamic.size() == 10 && !dynamic.empty() && dynamic.data() == numbers.data() &&
              tail[0] == 8 && window[0] == 5 && window.size() == 2 && suffix.size() == 3 &&
              dynamic.subspan(10).empty(),
          "dynamic subviews");
    check(sum(dynamic) == 55 && sum(fixed) == 1 + 20 + 30 + 40 + 5 + 6 && sum(prime_view) == 28,
          "conversion to a span of const");
    check(std::vector<int>(window.rbegin(), window.rend()) == std::vector<int>{6, 5},
          "reverse iteration");

    const std::vector<int> constant = {4, 5};
    auto const_view = tcb::make_span(constant);
    static_assert(std::is_same<decltype(const_view), tcb::span<const int>>::value,
                  "make_span over a const container");
    check(const_view.size() == 2 && const_view[1] == 5, "make_span over a const container");

    std::array<std::uint32_t, 2> words = {{0, 0x01020304u}};
    auto word_view = tcb::make_span(words);
    static_assert(std::is_same<decltype(word_view), tcb::span<std::uint32_t, 2>>::value &&
                      std::tuple_size<decltype(word_view)>::value == 2,
                  "make_span over a std::array");
    auto writable = tcb::as_writable_bytes(word_view.first<1>());
    static_assert(decltype(writable)::extent == sizeof(std::uint32_t), "static byte extent");
    for (auto& b : writable)
        b = tcb::byte(0xFF);
    auto bytes = tcb::as_bytes(word_view);
    check(words[0] == 0xFFFFFFFFu && bytes.size() == 2 * sizeof(std::uint32_t) &&
              bytes[0] == tcb::byte(0xFF) && bytes[sizeof(std::uint32_t) - 1] == tcb::byte(0xFF),
          "byte views");
    check(tcb::get<1>(word_view) == 0x01020304u && &tcb::get<0>(word_view) == &words[0],
          "tcb::get");

    if (failures != 0)
        return 1;
    std::puts("tcb-span ok");
    return 0;
}
