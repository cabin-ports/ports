// Checked arithmetic is exact: the result is stored wrapped and the return
// value reports whether the exact result did not fit.
#include <jtckdint.h>

#include <cstdint>
#include <cstdio>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

int main() {
    std::uint32_t c = 0;
    std::int8_t s = 0;
    std::uint8_t u = 0;
    std::int32_t m = 0;
    std::int64_t w = 0;

    // Upstream's documented example: 0x7fffffff + 2 fits uint32_t.
    CHECK(!ckd_add(&c, std::int32_t(0x7fffffff), std::int32_t(2)));
    CHECK(c == 0x80000001u);

    CHECK(ckd_add(&s, std::int8_t(100), std::int8_t(100)));
    CHECK(s == std::int8_t(-56));
    CHECK(!ckd_add(&s, std::int8_t(100), std::int8_t(27)));
    CHECK(s == 127);

    CHECK(ckd_sub(&u, std::uint8_t(1), std::uint8_t(2)));
    CHECK(u == 255);
    CHECK(!ckd_sub(&u, std::uint8_t(2), std::uint8_t(1)));
    CHECK(u == 1);

    CHECK(ckd_mul(&m, std::int32_t(65536), std::int32_t(65536)));
    CHECK(m == 0);
    CHECK(!ckd_mul(&m, std::int32_t(46340), std::int32_t(46340)));
    CHECK(m == 2147395600);

    CHECK(ckd_add(&w, INT64_MAX, std::int64_t(1)));
    CHECK(w == INT64_MIN);
    CHECK(!ckd_sub(&w, INT64_MIN, std::int64_t(-1)));
    CHECK(w == INT64_MIN + 1);

    CHECK(ckd_mul(&w, INT64_MAX, std::int64_t(2)));
    CHECK(w == -2);
    CHECK(!ckd_mul(&w, std::int64_t(3037000499), std::int64_t(3037000499)));
    CHECK(w == 9223372030926249001);

    CHECK(ckd_mul(&c, std::int32_t(-1), std::int32_t(1)));
    CHECK(c == 0xffffffffu);

    if (failures) return 1;
    std::puts("jtckdint C++ ok");
    return 0;
}
