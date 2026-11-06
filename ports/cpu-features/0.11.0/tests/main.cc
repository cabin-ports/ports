// Consumer test from C++: the same API through namespace cpu_features, the
// headers' C++ face.  Every feature has a distinct name that maps back to
// its enumerator, the value getter reads only the named feature, and the
// features the compiler's target guarantees are reported.
#include <cpu_features_macros.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#if defined(CPU_FEATURES_ARCH_X86)
#include <cpuinfo_x86.h>
#elif defined(CPU_FEATURES_ARCH_AARCH64)
#include <cpuinfo_aarch64.h>
#else
#error "this consumer test covers x86 and AArch64"
#endif

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

}  // namespace

int main() {
  std::map<std::string, int> names;
  int set = 0;

#if defined(CPU_FEATURES_ARCH_X86)
  const cpu_features::X86Info info = cpu_features::GetX86Info();
  for (int e = 0; e < cpu_features::X86_LAST_; ++e) {
    const cpu_features::X86FeaturesEnum feature =
        static_cast<cpu_features::X86FeaturesEnum>(e);
    names[cpu_features::GetX86FeaturesEnumName(feature)] = e;
    if (cpu_features::GetX86FeaturesEnumValue(&info.features, feature)) ++set;
  }
  CHECK(names.size() == static_cast<std::size_t>(cpu_features::X86_LAST_));
  CHECK(names.count("sse2") == 1 && names["sse2"] == cpu_features::X86_SSE2);
  CHECK(names.count("avx512f") == 1 &&
        names["avx512f"] == cpu_features::X86_AVX512F);
  CHECK(std::strlen(info.vendor) == 12);
  cpu_features::X86Features one = cpu_features::X86Features();
  one.avx512f = -1;  // a set signed one-bit field
  for (int e = 0; e < cpu_features::X86_LAST_; ++e)
    CHECK((cpu_features::GetX86FeaturesEnumValue(
               &one, static_cast<cpu_features::X86FeaturesEnum>(e)) != 0) ==
          (e == cpu_features::X86_AVX512F));
#if defined(__x86_64__) || defined(_M_X64) || defined(__SSE2__) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
  CHECK(info.features.sse2 != 0);
#endif
  const cpu_features::CacheInfo caches = cpu_features::GetX86CacheInfo();
  CHECK(caches.size > 0 && caches.size <= CPU_FEATURES_MAX_CACHE_LEVEL);
#else
  const cpu_features::Aarch64Info info = cpu_features::GetAarch64Info();
  for (int e = 0; e < cpu_features::AARCH64_LAST_; ++e) {
    const cpu_features::Aarch64FeaturesEnum feature =
        static_cast<cpu_features::Aarch64FeaturesEnum>(e);
    names[cpu_features::GetAarch64FeaturesEnumName(feature)] = e;
    if (cpu_features::GetAarch64FeaturesEnumValue(&info.features, feature))
      ++set;
  }
  CHECK(names.size() ==
        static_cast<std::size_t>(cpu_features::AARCH64_LAST_));
  CHECK(names.count("asimd") == 1 &&
        names["asimd"] == cpu_features::AARCH64_ASIMD);
  CHECK(names.count("sha2") == 1 && names["sha2"] == cpu_features::AARCH64_SHA2);
  cpu_features::Aarch64Features one = cpu_features::Aarch64Features();
  one.asimd = -1;  // a set signed one-bit field
  for (int e = 0; e < cpu_features::AARCH64_LAST_; ++e)
    CHECK((cpu_features::GetAarch64FeaturesEnumValue(
               &one, static_cast<cpu_features::Aarch64FeaturesEnum>(e)) != 0) ==
          (e == cpu_features::AARCH64_ASIMD));
#if defined(__ARM_NEON) || defined(_M_ARM64)
  CHECK(info.features.fp != 0 && info.features.asimd != 0);
#endif
#endif
  CHECK(names.count("unknown_feature") == 0);
  CHECK(set > 0);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("%d features reported\n", set);
  std::puts("cpu-features C++ consumer test passed");
  return 0;
}
