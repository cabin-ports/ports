#include <ittapi.hpp>
#include <jitprofiling.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                     \
    }                                                               \
  } while (0)

int main() {
  // An inherited collector setting would change the no-collector results.
  static const char *const vars[] = {"INTEL_LIBITTNOTIFY32", "INTEL_LIBITTNOTIFY64", "INTEL_ITTNOTIFY_GROUPS", "INTEL_JIT_PROFILER32", "INTEL_JIT_PROFILER64", "KMP_FOR_TPROFILE", "KMP_FOR_TCHECK"};
  for (std::size_t i = 0; i < sizeof vars / sizeof vars[0]; ++i)
#ifdef _WIN32
    _putenv_s(vars[i], "");
#else
    unsetenv(vars[i]);
#endif

  CHECK(__itt_get_collection_state() == __itt_collection_collector_absent);

  ittapi::Domain d{"cabin.cpp"};
  CHECK(d.valid());
  CHECK(d.get() == ittapi::Domain{"cabin.other"}.get());

  {
    auto t = d.task("work");
    CHECK(t.active());
    t.end();
    CHECK(!t.active());
  }
  {
    ittapi::ScopedPause p;
    CHECK(p.active());
    p.resume_now();
    CHECK(!p.active());
  }
  CHECK(iJIT_IsProfilingActive() == iJIT_NOTHING_RUNNING);
  return 0;
}
