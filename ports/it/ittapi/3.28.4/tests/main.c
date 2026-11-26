/* Without a collector every ITT call is a no-op, so this checks the stub's
   fallbacks: collection state, the shared dummy domain, NULL string handles,
   the JIT API. */
#define _POSIX_C_SOURCE 200112L
#include <ittnotify.h>
#include <jitprofiling.h>

#include <stdio.h>
#include <stdlib.h>

#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                     \
    }                                                               \
  } while (0)

int main(void) {
  /* An inherited collector setting would change the no-collector results. */
  static const char *const vars[] = {"INTEL_LIBITTNOTIFY32", "INTEL_LIBITTNOTIFY64", "INTEL_ITTNOTIFY_GROUPS", "INTEL_JIT_PROFILER32", "INTEL_JIT_PROFILER64", "KMP_FOR_TPROFILE", "KMP_FOR_TCHECK"};
  for (unsigned i = 0; i < sizeof vars / sizeof vars[0]; ++i)
#ifdef _WIN32
    _putenv_s(vars[i], "");
#else
    unsetenv(vars[i]);
#endif

  CHECK(__itt_get_collection_state() == __itt_collection_collector_absent);

  __itt_domain *d = __itt_domain_create("cabin.test");
  CHECK(d != NULL);
  CHECK(d->flags == 0);
  CHECK(__itt_domain_create("cabin.other") == d);
  CHECK(__itt_domain_create(NULL) == NULL);

  __itt_string_handle *h = __itt_string_handle_create("work");
  CHECK(h == NULL);

  __itt_pause();
  __itt_resume();
  __itt_task_begin(d, __itt_null, __itt_null, h);
  __itt_task_end(d);
  __itt_frame_begin_v3(d, NULL);
  __itt_frame_end_v3(d, NULL);
  __itt_thread_set_name("cabin");
  CHECK(__itt_get_collection_state() == __itt_collection_collector_absent);

  CHECK(iJIT_IsProfilingActive() == iJIT_NOTHING_RUNNING);
  CHECK(iJIT_GetNewMethodID() == 1);
  CHECK(iJIT_GetNewMethodID() == 2);
  iJIT_Method_Load m = {0};
  m.method_id = 1;
  CHECK(iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, &m) == 0);
  return 0;
}
