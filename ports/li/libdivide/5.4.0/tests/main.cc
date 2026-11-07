// Opt into the vector API the host has, as the README shows.
#if defined(__x86_64__) || defined(_M_X64)
#define LIBDIVIDE_SSE2
#elif defined(__aarch64__)
#define LIBDIVIDE_NEON
#endif
#include <libdivide.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;

std::uint64_t rng_state = 0x9E3779B97F4A7C15u;

// xorshift64*, shifted down by a random amount so small magnitudes occur.
std::uint64_t next_random() {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    std::uint64_t x = rng_state * 2685821657736338717u;
    return x >> (x & 63);
}

const std::int64_t edges[] = {0,
                              1,
                              2,
                              3,
                              5,
                              7,
                              10,
                              641,
                              1000003,
                              -1,
                              -2,
                              -3,
                              -7,
                              -641,
                              std::numeric_limits<std::int64_t>::max(),
                              std::numeric_limits<std::int64_t>::min(),
                              std::numeric_limits<std::int64_t>::max() - 1,
                              std::numeric_limits<std::int64_t>::min() + 1,
                              std::numeric_limits<std::int32_t>::max(),
                              std::numeric_limits<std::int32_t>::min(),
                              std::numeric_limits<std::uint32_t>::max(),
                              std::numeric_limits<std::int16_t>::max(),
                              std::numeric_limits<std::int16_t>::min(),
                              std::numeric_limits<std::uint16_t>::max(),
                              std::int64_t(1) << 31,
                              std::int64_t(1) << 32,
                              std::int64_t(1) << 40};

void fail(const char* what, unsigned long long n, unsigned long long d) {
    std::printf("%s failed for n=%#llx d=%#llx\n", what, n, d);
    ++failures;
}

// operator/ and operator/= through divider<T, ALGO> agree with T's own
// division over the edge values and random numerators, and recover()
// returns d.  MIN / -1 has no representable quotient and is skipped.
template <typename T, libdivide::Branching ALGO>
void check(T d) {
    const libdivide::divider<T, ALGO> fast(d);
    if (fast.recover() != d) fail("recover", 0, static_cast<unsigned long long>(d));
    for (std::size_t i = 0; i < sizeof edges / sizeof edges[0] + 200; ++i) {
        T n = static_cast<T>(i < sizeof edges / sizeof edges[0] ? static_cast<std::uint64_t>(edges[i])
                                                                 : next_random());
        if (std::numeric_limits<T>::is_signed && n == std::numeric_limits<T>::min() &&
            d == static_cast<T>(-1))
            continue;
        T want = static_cast<T>(n / d);
        T got = n / fast;
        T in_place = n;
        in_place /= fast;
        if (got != want || in_place != want)
            fail(ALGO == libdivide::BRANCHFREE ? "branchfree" : "branchfull",
                 static_cast<unsigned long long>(n), static_cast<unsigned long long>(d));
    }
}

template <typename T>
void check_type(std::uint64_t bits) {
    T d = static_cast<T>(bits);
    if (d == 0) return;
    check<T, libdivide::BRANCHFULL>(d);
    // An unsigned branchfree divider rejects 1.
    if (std::numeric_limits<T>::is_signed || d != 1) check<T, libdivide::BRANCHFREE>(d);
}

void check_divisor(std::uint64_t bits) {
    check_type<std::int16_t>(bits);
    check_type<std::uint16_t>(bits);
    check_type<std::int32_t>(bits);
    check_type<std::uint32_t>(bits);
    check_type<std::int64_t>(bits);
    check_type<std::uint64_t>(bits);
}

#if defined(LIBDIVIDE_SSE2) || defined(LIBDIVIDE_NEON)
// The vector operator/ and operator/= overloads, one lane width each.
void check_vectors() {
    std::int32_t s32_in[4] = {std::numeric_limits<std::int32_t>::min() + 1, -100, 99,
                              std::numeric_limits<std::int32_t>::max()};
    std::int32_t s32_out[4];
    std::uint64_t u64_in[2] = {std::numeric_limits<std::uint64_t>::max(), 1000000007u};
    std::uint64_t u64_out[2];
    const libdivide::divider<std::int32_t> s32_d(-7);
    const libdivide::branchfree_divider<std::uint64_t> u64_d(12345);
#if defined(LIBDIVIDE_SSE2)
    __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s32_in));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(s32_out), v / s32_d);
    v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(u64_in));
    v /= u64_d;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(u64_out), v);
#else
    vst1q_s32(s32_out, vld1q_s32(s32_in) / s32_d);
    uint64x2_t v = vld1q_u64(u64_in);
    v /= u64_d;
    vst1q_u64(u64_out, v);
#endif
    for (int i = 0; i < 4; ++i)
        if (s32_out[i] != s32_in[i] / -7) fail("s32 vector", static_cast<unsigned long long>(s32_in[i]),
                                                static_cast<unsigned long long>(-7));
    for (int i = 0; i < 2; ++i)
        if (u64_out[i] != u64_in[i] / 12345) fail("u64 vector", u64_in[i], 12345);
}
#endif

}  // namespace

int main() {
    static_assert(std::is_same<libdivide::branchfree_divider<std::uint32_t>,
                               libdivide::divider<std::uint32_t, libdivide::BRANCHFREE>>::value,
                  "branchfree_divider is divider<T, BRANCHFREE>");

    for (std::int64_t e : edges) check_divisor(static_cast<std::uint64_t>(e));
    for (int i = 0; i < 200; ++i) check_divisor(next_random());

    const libdivide::divider<std::int32_t> seven(7);
    if (!(seven == libdivide::divider<std::int32_t>(7)) || seven != libdivide::divider<std::int32_t>(7) ||
        seven == libdivide::divider<std::int32_t>(-7) || !(seven != libdivide::divider<std::int32_t>(9))) {
        std::printf("divider comparison failed\n");
        ++failures;
    }

    // The README's example: one divider reused across a vector.
    std::vector<std::int64_t> numbers;
    for (std::int64_t i = 0; i < 100; ++i) numbers.push_back(i * i * 7919 - 40000);
    const libdivide::divider<std::int64_t> fast_d(-37);
    std::int64_t drift = 0;
    for (std::int64_t n : numbers) drift += n / fast_d - n / -37;
    if (drift != 0) {
        std::printf("vector division drifted by %lld\n", static_cast<long long>(drift));
        ++failures;
    }

#if defined(LIBDIVIDE_SSE2) || defined(LIBDIVIDE_NEON)
    check_vectors();
#endif

    if (failures != 0) {
        std::printf("libdivide: %d failures\n", failures);
        return 1;
    }
    std::printf("libdivide: ok\n");
    return 0;
}
