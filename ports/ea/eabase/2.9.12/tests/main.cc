// Exercises EABase's C++ surface: 128-bit integer arithmetic, offsetof and
// array-count helpers, size units, result codes, and the instruction-pointer
// macro of the post-2.09.12 headers that EASTL 3.27 uses.  C++11, the
// interface standard the port declares (C++14 on MSVC).  Division is left
// out: int128.h's Modulus sets each bit of a quotient it never initialized.
#include <EABase/eabase.h>
#include <EABase/earesult.h>
#include <EABase/eaunits.h>

#include <cstddef>
#include <cstdio>

#if !defined(EA_COMPILER_CPP11_ENABLED)
#error "EABase must detect C++11"
#endif
#if !defined(EA_CPP20_CONSTEXPR) || !defined(EA_GET_INSTRUCTION_POINTER)
#error "the headers predate master 0699a15"
#endif

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "eabase C++ test failed: %s\n", what);
        ++failures;
    }
}

struct Record {
    char tag;
    double value;
    int count;
};

void test_uint128() {
    uint128_t max64(UINT64_C(0xFFFFFFFFFFFFFFFF));
    // (2^64 - 1)^2 = 2^128 - 2^65 + 1: high half 0xFF..FE, low half 1.
    uint128_t square = max64 * max64;
    check(static_cast<unsigned long long>(square) == 1, "low half of the square");
    check(static_cast<unsigned long long>(square >> 64) == UINT64_C(0xFFFFFFFFFFFFFFFE),
          "high half of the square");
    check(square.GetBit(127) == 1 && square.GetBit(64) == 0 && square.GetBit(0) == 1,
          "GetBit");
    check(square > max64 && max64 < square, "comparison");

    uint128_t one(UINT64_C(1));
    uint128_t high = one << 100;
    check(high.GetBit(100) == 1 && (high >> 100) == one, "shifts across the halves");
    check(uint128_t(UINT64_C(0), UINT64_C(1)) == (one << 64), "two-part constructor");
}

void test_int128() {
    int128_t five(UINT64_C(5));
    int128_t three(UINT64_C(3));
    int128_t zero(UINT64_C(0));
    int128_t minus_five = -five;
    int128_t product = minus_five * three;
    check(product == -int128_t(UINT64_C(15)), "negative product");
    check(static_cast<long long>(product) == -15, "conversion of a negative value");
    check(product < zero && zero < five, "signed comparison");
    check(product + int128_t(UINT64_C(15)) == zero, "addition back to zero");
}

}  // namespace

int main() {
    test_uint128();
    test_int128();

    check(EA_OFFSETOF(Record, count) == offsetof(Record, count), "EA_OFFSETOF");
    int values[7] = {0};
    static_assert(EAArrayCount(values) == 7, "EAArrayCount is a constant expression");
    check(EA_ALIGN_OF(int) == alignof(int), "EA_ALIGN_OF");

    check(EA_KILOBYTE(16) == 16000u && EA_KIBIBYTE(16) == 16384u && EA_MEBIBYTE(8) == 8388608u,
          "size units");
    check(EA_SUCCEEDED(EA::EA_SUCCESS) && EA_FAILED(EA::EA_FAILURE), "result codes");

    void* ip = EA_GET_INSTRUCTION_POINTER();
    check(ip != nullptr, "EA_GET_INSTRUCTION_POINTER");

    if (failures != 0)
        return 1;
    std::printf("eabase %s C++ ok\n", EABASE_VERSION);
    return 0;
}
