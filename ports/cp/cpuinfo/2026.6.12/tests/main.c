#include <cpuinfo.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,       \
              #cond);                                                        \
      exit(1);                                                               \
    }                                                                        \
  } while (0)

/* i lies in [start, start + count). */
static int within(uint32_t i, uint32_t start, uint32_t count) {
  return i >= start && i - start < count;
}

static void check_topology(void) {
  const uint32_t nproc = cpuinfo_get_processors_count();
  const uint32_t ncore = cpuinfo_get_cores_count();
  const uint32_t ncluster = cpuinfo_get_clusters_count();
  const uint32_t npackage = cpuinfo_get_packages_count();
  CHECK(nproc >= 1);
  CHECK(ncore >= 1 && ncore <= nproc);
  CHECK(ncluster >= 1 && ncluster <= ncore);
  CHECK(npackage >= 1 && npackage <= ncluster);

  const struct cpuinfo_processor* processors = cpuinfo_get_processors();
  const struct cpuinfo_core* cores = cpuinfo_get_cores();
  const struct cpuinfo_cluster* clusters = cpuinfo_get_clusters();
  const struct cpuinfo_package* packages = cpuinfo_get_packages();
  for (uint32_t i = 0; i < nproc; i++) {
    const struct cpuinfo_processor* p = cpuinfo_get_processor(i);
    CHECK(p == &processors[i]);
    CHECK(p->core >= cores && p->core < cores + ncore);
    CHECK(within(i, p->core->processor_start, p->core->processor_count));
    CHECK(p->cluster >= clusters && p->cluster < clusters + ncluster);
    CHECK(within(i, p->cluster->processor_start, p->cluster->processor_count));
    CHECK(p->package >= packages && p->package < packages + npackage);
    CHECK(within(i, p->package->processor_start, p->package->processor_count));
    CHECK(p->core->package == p->package);
  }

  /* Each core's processors point back to it, so the cores partition the
   * processors; each core names its vendor. */
  uint32_t covered = 0;
  for (uint32_t i = 0; i < ncore; i++) {
    const struct cpuinfo_core* core = cpuinfo_get_core(i);
    CHECK(core == &cores[i]);
    CHECK(core->processor_start < nproc && core->processor_count >= 1);
    CHECK(core->processor_count <= nproc - core->processor_start);
    for (uint32_t j = 0; j < core->processor_count; j++) {
      CHECK(processors[core->processor_start + j].core == core);
    }
    covered += core->processor_count;
    CHECK(core->vendor != cpuinfo_vendor_unknown);
  }
  CHECK(covered == nproc);

  const uint32_t nuarch = cpuinfo_get_uarchs_count();
  CHECK(nuarch >= 1);
  for (uint32_t i = 0; i < nuarch; i++) {
    const struct cpuinfo_uarch_info* uarch = cpuinfo_get_uarch(i);
    CHECK(uarch != NULL);
    CHECK(uarch->processor_count >= 1 && uarch->processor_count <= nproc);
    CHECK(uarch->core_count >= 1 && uarch->core_count <= ncore);
  }
  CHECK(cpuinfo_get_current_uarch_index() < nuarch);
  CHECK(cpuinfo_get_current_uarch_index_with_default(nuarch) <= nuarch);

  const struct cpuinfo_processor* current = cpuinfo_get_current_processor();
  if (current != NULL) {
    CHECK(current >= processors && current < processors + nproc);
  }
}

static void check_l1d(void) {
  const uint32_t count = cpuinfo_get_l1d_caches_count();
  CHECK(count >= 1 && count <= cpuinfo_get_processors_count());
  const struct cpuinfo_cache* caches = cpuinfo_get_l1d_caches();
  for (uint32_t i = 0; i < count; i++) {
    const struct cpuinfo_cache* cache = cpuinfo_get_l1d_cache(i);
    CHECK(cache == &caches[i]);
    CHECK(cache->size != 0 && cache->processor_count >= 1);
    CHECK(cache->line_size >= 16 && cache->line_size <= 128);
    CHECK((cache->line_size & (cache->line_size - 1)) == 0);
  }
  for (uint32_t i = 0; i < cpuinfo_get_processors_count(); i++) {
    const struct cpuinfo_cache* l1d = cpuinfo_get_processor(i)->cache.l1d;
    if (l1d != NULL) {
      CHECK(l1d >= caches && l1d < caches + count);
      CHECK(within(i, l1d->processor_start, l1d->processor_count));
    }
  }
}

/* An ISA bit that initialization detects rather than the target implies:
 * SSE3 from CPUID on x86, LSE atomics from sysctl on Apple silicon, and
 * CRC32 from the OS on other AArch64.  Every CI runner's CPU has it. */
static void check_isa(void) {
#if CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64
  CHECK(cpuinfo_has_x86_sse3());
#elif CPUINFO_ARCH_ARM64 && defined(__APPLE__)
  CHECK(cpuinfo_has_arm_atomics());
#elif CPUINFO_ARCH_ARM64
  CHECK(cpuinfo_has_arm_crc32());
#endif
}

int main(void) {
  CHECK(cpuinfo_initialize());
  check_topology();
  check_l1d();
  check_isa();

  /* Deinitialization releases the tables and allows a fresh detection. */
  const uint32_t nproc = cpuinfo_get_processors_count();
  cpuinfo_deinitialize();
  CHECK(cpuinfo_initialize());
  CHECK(cpuinfo_get_processors_count() == nproc);
  check_topology();
  cpuinfo_deinitialize();

  puts("cpuinfo consumer ok");
  return 0;
}
