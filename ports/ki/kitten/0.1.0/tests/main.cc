#include <kitten/instances/optional.h>
#include <kitten/instances/sequence_container.h>
#include <kitten/kitten.h>

#include <cstdio>
#include <optional>
#include <vector>

using namespace rvarago::kitten;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s\n", #c); ++failures; } } while (0)

int main() {
    auto inc = [](int e) { return e + 1; };

    // fmap (operator|)
    CHECK((std::optional<int>{41} | inc) == std::optional<int>{42});
    CHECK(!(std::optional<int>{} | inc).has_value());
    CHECK((std::vector<int>{1, 2, 3} | inc) == (std::vector<int>{2, 3, 4}));

    // bind (operator>>)
    CHECK((std::optional<int>{20} >> [](int x) { return std::optional<int>{x + 22}; }) == std::optional<int>{42});
    CHECK(!(std::optional<int>{20} >> [](int) { return std::optional<int>{}; }).has_value());
    CHECK(!(std::optional<int>{} >> [](int x) { return std::optional<int>{x}; }).has_value());
    CHECK((std::vector<int>{1, 2} >> [](int x) { return std::vector<int>{x, x * 10}; }) ==
          (std::vector<int>{1, 10, 2, 20}));

    // combine and the wrapped-pair operator+
    auto add = [](int a, int b) { return a + b; };
    CHECK(combine(std::optional<int>{40}, std::optional<int>{2}, add) == std::optional<int>{42});
    CHECK(!combine(std::optional<int>{40}, std::optional<int>{}, add).has_value());
    CHECK(combine(std::vector<int>{1, 2}, std::vector<int>{10, 20}, add) == (std::vector<int>{11, 21, 12, 22}));
    CHECK((std::optional<int>{40} + std::optional<int>{2}) == std::optional<int>{42});
    CHECK((std::tuple{std::optional<int>{6}, std::optional<int>{7}} + [](int a, int b) { return a * b; }) ==
          std::optional<int>{42});

    // wrap / pure
    CHECK(wrap<std::optional>(42) == std::optional<int>{42});
    CHECK(pure<std::vector>(7) == std::vector<int>{7});

    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures;
}
