#include <cpuinfo.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>
#include <vector>

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,  \
                   #cond);                                                   \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

int main() {
  // Concurrent first calls run detection once (pthread_once or
  // InitOnceExecuteOnce) and all see the same tables.
  std::atomic<int> failures{0};
  std::vector<const cpuinfo_processor*> seen(4);
  std::vector<std::thread> threads;
  for (std::size_t t = 0; t < seen.size(); t++) {
    threads.emplace_back([&, t] {
      if (!cpuinfo_initialize()) {
        failures++;
      }
      seen[t] = cpuinfo_get_processors();
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  CHECK(failures == 0);
  for (const cpuinfo_processor* processors : seen) {
    CHECK(processors != nullptr && processors == seen[0]);
  }

  // The uarch entries count every core and processor once, and every
  // core's uarch has an entry.
  const std::uint32_t ncore = cpuinfo_get_cores_count();
  std::uint32_t uarch_cores = 0;
  std::uint32_t uarch_processors = 0;
  std::set<cpuinfo_uarch> uarchs;
  for (std::uint32_t i = 0; i < cpuinfo_get_uarchs_count(); i++) {
    const cpuinfo_uarch_info* info = cpuinfo_get_uarch(i);
    CHECK(info != nullptr);
    uarchs.insert(info->uarch);
    uarch_cores += info->core_count;
    uarch_processors += info->processor_count;
  }
  CHECK(uarch_cores == ncore);
  CHECK(uarch_processors == cpuinfo_get_processors_count());
  for (std::uint32_t i = 0; i < ncore; i++) {
    CHECK(uarchs.count(cpuinfo_get_core(i)->uarch) == 1);
  }

  // Package names are NUL-terminated within their fixed buffer.
  for (std::uint32_t i = 0; i < cpuinfo_get_packages_count(); i++) {
    const cpuinfo_package* package = cpuinfo_get_package(i);
    CHECK(std::memchr(package->name, '\0', CPUINFO_PACKAGE_NAME_MAX) !=
          nullptr);
    CHECK(package->core_count >= 1 && package->core_count <= ncore);
  }

  const cpuinfo_core* current = cpuinfo_get_current_core();
  if (current != nullptr) {
    const cpuinfo_core* cores = cpuinfo_get_cores();
    CHECK(current >= cores && current < cores + ncore);
  }

#if CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64
  CHECK(cpuinfo_has_x86_sse3());
#elif CPUINFO_ARCH_ARM64 && defined(__APPLE__)
  CHECK(cpuinfo_has_arm_atomics());
#elif CPUINFO_ARCH_ARM64
  CHECK(cpuinfo_has_arm_crc32());
#endif

  cpuinfo_deinitialize();
  std::puts("cpuinfo consumer ok");
  return 0;
}
