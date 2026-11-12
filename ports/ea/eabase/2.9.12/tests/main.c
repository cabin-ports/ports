/* Checks EABase's platform, compiler, and endianness detection against the
   target it runs on, its portable types and alignment macro, and its
   C static_assert.  C99, the interface standard the port declares (MSVC
   compiles it as C11). */
#include <EABase/eabase.h>
#include <EABase/eahave.h>
#include <EABase/version.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static_assert(sizeof(int32_t) == 4, "static_assert works in C");
static_assert(EABASE_VERSION_N == 20912, "EABASE_VERSION_N is 2.09.12");

/* The detected platform and compiler match the predefined macros. */
#if defined(__APPLE__) && !(defined(EA_PLATFORM_APPLE) && defined(EA_PLATFORM_POSIX))
#error "Apple platform not detected"
#elif defined(__linux__) && !(defined(EA_PLATFORM_LINUX) && defined(EA_PLATFORM_POSIX))
#error "Linux platform not detected"
#elif defined(__FreeBSD__) && !(defined(EA_PLATFORM_FREEBSD) && defined(EA_PLATFORM_POSIX))
#error "FreeBSD platform not detected"
#elif defined(_WIN32) && !(defined(EA_PLATFORM_WINDOWS) && defined(EA_PLATFORM_MICROSOFT))
#error "Windows platform not detected"
#endif
#if defined(__clang__) && !defined(EA_COMPILER_CLANG) && !defined(EA_COMPILER_CLANG_CL)
#error "Clang not detected"
#elif !defined(__clang__) && defined(__GNUC__) && !defined(EA_COMPILER_GNUC)
#error "GCC not detected"
#elif !defined(__clang__) && defined(_MSC_VER) && !defined(EA_COMPILER_MSVC)
#error "MSVC not detected"
#endif

#define FEATURE_ON EA_ENABLED
#define FEATURE_OFF EA_DISABLED

static int failures = 0;

static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "eabase C test failed: %s\n", what);
        ++failures;
    }
}

int main(void) {
    union {
        uint32_t word;
        uint8_t bytes[4];
    } probe;
    ssize_t minus_one = -1;
    EA_ALIGNED(int, aligned_value, 16);

    aligned_value = 0;
    check(strcmp(EABASE_VERSION, "2.09.12") == 0, "EABASE_VERSION");

    probe.word = 0x01020304u;
#if defined(EA_SYSTEM_LITTLE_ENDIAN) && !defined(EA_SYSTEM_BIG_ENDIAN)
    check(probe.bytes[0] == 0x04, "detected little-endian");
#elif defined(EA_SYSTEM_BIG_ENDIAN) && !defined(EA_SYSTEM_LITTLE_ENDIAN)
    check(probe.bytes[0] == 0x01, "detected big-endian");
#else
    check(0, "exactly one byte order is detected");
#endif
    check(EAArrayCount(probe.bytes) == 4, "EAArrayCount");

    check(EA_PLATFORM_PTR_SIZE == sizeof(void*), "EA_PLATFORM_PTR_SIZE");
    check(EA_WCHAR_SIZE == sizeof(wchar_t), "EA_WCHAR_SIZE");
    check(sizeof(char8_t) == 1 && sizeof(char16_t) == 2 && sizeof(char32_t) == 4,
          "char8_t, char16_t, and char32_t sizes");
    check(sizeof(bool8_t) == 1, "bool8_t size");
    check(sizeof(ssize_t) == sizeof(size_t) && minus_one < 0, "ssize_t is a signed size_t");
    check((uintptr_t)&aligned_value % 16 == 0 && aligned_value == 0, "EA_ALIGNED");

    check(EA_IS_ENABLED(FEATURE_ON) && !EA_IS_ENABLED(FEATURE_OFF), "EA_IS_ENABLED");
    check(strcmp(EA_STRINGIFY(EA_PREPROCESSOR_JOIN(ea, base)), "eabase") == 0,
          "EA_STRINGIFY and EA_PREPROCESSOR_JOIN");
    check(EA_LIKELY(probe.word == 0x01020304u) && !EA_UNLIKELY(probe.word != 0x01020304u),
          "EA_LIKELY and EA_UNLIKELY keep the condition's value");

    if (failures != 0)
        return 1;
    printf("eabase %s on %s\n", EABASE_VERSION, EA_PLATFORM_NAME);
    return 0;
}
