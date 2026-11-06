/* Consumer test from C: the running CPU's identification and the features
   the compiler's target guarantees, the feature introspection (each name
   and value getter reads its own feature), and on x86 the cache hierarchy
   and the microarchitecture classification, through the architecture's
   public header.  The CI runners are x86-64 (Linux, Windows) and AArch64
   (macOS). */
#include <cpu_features_macros.h>

#include <stdio.h>
#include <string.h>

#if defined(CPU_FEATURES_ARCH_X86)
#include <cpuinfo_x86.h>
#elif defined(CPU_FEATURES_ARCH_AARCH64)
#include <cpuinfo_aarch64.h>
#else
#error "this consumer test covers x86 and AArch64"
#endif

static int failures = 0;

static void check(int ok, const char* what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond) ? 1 : 0, #cond, __LINE__)

#if defined(CPU_FEATURES_ARCH_X86)
/* A NUL within the first `size` bytes. */
static int terminated(const char* s, size_t size) {
  return memchr(s, '\0', size) != NULL;
}
#endif

int main(void) {
#if defined(CPU_FEATURES_ARCH_X86)
  const X86Info info = GetX86Info();
  int e, set = 0;

  CHECK(terminated(info.vendor, sizeof info.vendor));
  CHECK(strlen(info.vendor) == 12); /* e.g. GenuineIntel, AuthenticAMD */
  CHECK(terminated(info.brand_string, sizeof info.brand_string));
  CHECK(info.family > 0);

  /* x86-64 implies x87, SSE and SSE2; so does a compiler told it may use
     SSE2 on 32-bit x86. */
#if defined(__x86_64__) || defined(_M_X64) || defined(__SSE2__) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
  CHECK(info.features.fpu && info.features.sse && info.features.sse2);
  CHECK(GetX86FeaturesEnumValue(&info.features, X86_SSE2) != 0);
#endif
  /* AVX2 is read under the same OS-state check as AVX. */
  CHECK(!info.features.avx2 || info.features.avx);

  for (e = 0; e < X86_LAST_; ++e) {
    const char* name = GetX86FeaturesEnumName((X86FeaturesEnum)e);
    CHECK(name != NULL && name[0] != '\0' &&
          strcmp(name, "unknown_feature") != 0);
    if (GetX86FeaturesEnumValue(&info.features, (X86FeaturesEnum)e)) ++set;
  }
  CHECK(set > 0);
  CHECK(strcmp(GetX86FeaturesEnumName(X86_SSE2), "sse2") == 0);
  CHECK(strcmp(GetX86FeaturesEnumName(X86_AVX2), "avx2") == 0);
  CHECK(strcmp(GetX86FeaturesEnumName(X86_LAST_), "unknown_feature") == 0);
  CHECK(GetX86FeaturesEnumValue(&info.features, X86_LAST_) == 0);

  {
    /* The value getter reads the named feature and only that one. */
    X86Features one;
    memset(&one, 0, sizeof one);
    one.avx2 = -1; /* a set signed one-bit field */
    for (e = 0; e < X86_LAST_; ++e)
      CHECK((GetX86FeaturesEnumValue(&one, (X86FeaturesEnum)e) != 0) ==
            (e == X86_AVX2));
  }

  {
    const X86Microarchitecture uarch = GetX86Microarchitecture(&info);
    const char* uarch_name = GetX86MicroarchitectureName(uarch);
    X86Info cpu;
    CHECK(uarch_name != NULL && uarch_name[0] != '\0');

    /* Classification by vendor, family, model and stepping. */
    memset(&cpu, 0, sizeof cpu);
    strcpy(cpu.vendor, CPU_FEATURES_VENDOR_GENUINE_INTEL);
    cpu.family = 0x06;
    cpu.model = 0x55;
    cpu.stepping = 7;
    CHECK(GetX86Microarchitecture(&cpu) == INTEL_CCL);
    cpu.stepping = 4;
    CHECK(GetX86Microarchitecture(&cpu) == INTEL_SKL);
    strcpy(cpu.vendor, CPU_FEATURES_VENDOR_AUTHENTIC_AMD);
    cpu.family = 0x19;
    cpu.model = 0x21;
    CHECK(GetX86Microarchitecture(&cpu) == AMD_ZEN3);
    CHECK(strcmp(GetX86MicroarchitectureName(AMD_ZEN3), "AMD_ZEN3") == 0);
    strcpy(cpu.vendor, "NoSuchVendor");
    CHECK(GetX86Microarchitecture(&cpu) == X86_UNKNOWN);
  }

  {
    const CacheInfo caches = GetX86CacheInfo();
    int i, data = 0;
    CHECK(caches.size > 0 && caches.size <= CPU_FEATURES_MAX_CACHE_LEVEL);
    for (i = 0; i < caches.size && i < CPU_FEATURES_MAX_CACHE_LEVEL; ++i) {
      const CacheLevelInfo* level = &caches.levels[i];
      CHECK(level->level >= 1);
      if (level->cache_type == CPU_FEATURE_CACHE_DATA ||
          level->cache_type == CPU_FEATURE_CACHE_UNIFIED) {
        CHECK(level->cache_size > 0 && level->line_size > 0);
        if (level->level == 1) ++data;
      }
    }
    CHECK(data > 0); /* a first-level data or unified cache */
  }

  printf("x86 %s family %d model %d: %d features\n", info.vendor, info.family,
         info.model, set);
#else
  const Aarch64Info info = GetAarch64Info();
  int e, set = 0;

  /* AArch64 with Advanced SIMD (always so on macOS and Windows) has the FP
     and SIMD units. */
#if defined(__ARM_NEON) || defined(_M_ARM64)
  CHECK(info.features.fp && info.features.asimd);
  CHECK(GetAarch64FeaturesEnumValue(&info.features, AARCH64_ASIMD) != 0);
#endif

  for (e = 0; e < AARCH64_LAST_; ++e) {
    const char* name = GetAarch64FeaturesEnumName((Aarch64FeaturesEnum)e);
    CHECK(name != NULL && name[0] != '\0' &&
          strcmp(name, "unknown_feature") != 0);
    if (GetAarch64FeaturesEnumValue(&info.features, (Aarch64FeaturesEnum)e))
      ++set;
  }
  CHECK(set > 0);
  CHECK(strcmp(GetAarch64FeaturesEnumName(AARCH64_FP), "fp") == 0);
  CHECK(strcmp(GetAarch64FeaturesEnumName(AARCH64_ASIMD), "asimd") == 0);
  CHECK(strcmp(GetAarch64FeaturesEnumName(AARCH64_LAST_), "unknown_feature") ==
        0);
  CHECK(GetAarch64FeaturesEnumValue(&info.features, AARCH64_LAST_) == 0);

  {
    /* The value getter reads the named feature and only that one. */
    Aarch64Features one;
    memset(&one, 0, sizeof one);
    one.sha2 = -1; /* a set signed one-bit field */
    for (e = 0; e < AARCH64_LAST_; ++e)
      CHECK((GetAarch64FeaturesEnumValue(&one, (Aarch64FeaturesEnum)e) != 0) ==
            (e == AARCH64_SHA2));
  }

  printf("aarch64 implementer %d part %d: %d features\n", info.implementer,
         info.part, set);
#endif

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("cpu-features C consumer test passed");
  return 0;
}
