#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif

#include <wtr/watcher-c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define SEP "\\"
typedef SRWLOCK lock_type;
static void init_lock(lock_type *l) { InitializeSRWLock(l); }
static void lock(lock_type *l) { AcquireSRWLockExclusive(l); }
static void unlock(lock_type *l) { ReleaseSRWLockExclusive(l); }
static void pause_ms(unsigned ms) { Sleep(ms); }
#else
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define SEP "/"
typedef pthread_mutex_t lock_type;
static void init_lock(lock_type *l) { pthread_mutex_init(l, NULL); }
static void lock(lock_type *l) { pthread_mutex_lock(l); }
static void unlock(lock_type *l) { pthread_mutex_unlock(l); }
static void pause_ms(unsigned ms) {
  struct timespec t;
  t.tv_sec = ms / 1000;
  t.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&t, NULL);
}
#endif

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

#define PATH_LEN 1024

/* What the callback, which runs on the watcher's thread, has seen.  It
   compares the borrowed event strings while they are valid. */
struct seen {
  lock_type lock;
  char live[PATH_LEN];
  char die[PATH_LEN];
  char target[PATH_LEN];
  char renamed[PATH_LEN];
  int events;
  int live_ok;
  int file_creates;
  int target_created;
  int rename_ok;
  int last_is_die;
  long long live_time;
};

static void on_event(struct wtr_watcher_event e, void *context) {
  struct seen *s = (struct seen *)context;
  int is_die = e.effect_type == WTR_WATCHER_EFFECT_DESTROY &&
               e.path_type == WTR_WATCHER_PATH_WATCHER &&
               strcmp(e.path_name, s->die) == 0;
  lock(&s->lock);
  ++s->events;
  if (s->events == 1) {
    s->live_ok = e.effect_type == WTR_WATCHER_EFFECT_CREATE &&
                 e.path_type == WTR_WATCHER_PATH_WATCHER &&
                 strcmp(e.path_name, s->live) == 0 &&
                 e.associated_path_name == NULL;
    s->live_time = (long long)e.effect_time;
  }
  if (e.effect_type == WTR_WATCHER_EFFECT_CREATE &&
      e.path_type == WTR_WATCHER_PATH_FILE) {
    ++s->file_creates;
    if (strcmp(e.path_name, s->target) == 0)
      s->target_created = 1;
  }
  if (e.effect_type == WTR_WATCHER_EFFECT_RENAME &&
      e.path_type == WTR_WATCHER_PATH_FILE && e.associated_path_name != NULL &&
      strcmp(e.path_name, s->target) == 0 &&
      strcmp(e.associated_path_name, s->renamed) == 0)
    s->rename_ok = 1;
  s->last_is_die = is_die;
  unlock(&s->lock);
}

/* Polls `*flag` every 10 ms, at most `ticks` times. */
static int wait_flag(struct seen *s, int *flag, int ticks) {
  int i, value = 0;
  for (i = 0; i < ticks && !value; ++i) {
    lock(&s->lock);
    value = *flag;
    unlock(&s->lock);
    if (!value)
      pause_ms(10);
  }
  return value;
}

static int touch(char const *path) {
  FILE *f = fopen(path, "w");
  if (f == NULL)
    return 0;
  fputs("cabin", f);
  return fclose(f) == 0;
}

/* Makes a fresh directory under the system's temporary directory, its path
   resolved (FSEvents reports real paths, and macOS's temporary directory
   sits behind the /var symlink), and gives its name as events report it. */
static int make_dir(char *dir, char *reported) {
#if defined(_WIN32)
  char base[MAX_PATH + 1];
  wchar_t wide[PATH_LEN];
  DWORD n = GetTempPathA(sizeof base, base);
  if (n == 0 || n > MAX_PATH)
    return 0;
  snprintf(dir, PATH_LEN, "%scabin-watcher-c-%lu", base,
           (unsigned long)GetCurrentProcessId());
  if (!CreateDirectoryA(dir, NULL))
    return 0;
  /* wtr_watcher_open reads the ANSI code page, like the A functions, and
     events carry UTF-8. */
  return MultiByteToWideChar(CP_ACP, 0, dir, -1, wide, PATH_LEN) != 0 &&
         WideCharToMultiByte(CP_UTF8, 0, wide, -1, reported, PATH_LEN, NULL,
                             NULL) != 0;
#else
  char made[PATH_LEN];
  char *real;
  char const *base = getenv("TMPDIR");
  if (base == NULL || *base == '\0')
    base = "/tmp";
  snprintf(made, sizeof made, "%s/cabin-watcher-c-%ld", base, (long)getpid());
  if (mkdir(made, 0700) != 0)
    return 0;
  real = realpath(made, NULL);
  if (real == NULL)
    return 0;
  snprintf(dir, PATH_LEN, "%s", real);
  snprintf(reported, PATH_LEN, "%s", real);
  free(real);
  return 1;
#endif
}

int main(void) {
  static struct seen s;
  char dir[PATH_LEN];
  char reported[PATH_LEN];
  char probe[PATH_LEN];
  char target[PATH_LEN];
  char renamed[PATH_LEN];
  void *watcher;
  int i, created = 0, ready = 0;

  init_lock(&s.lock);
  if (!make_dir(dir, reported)) {
    fprintf(stderr, "cannot create a temporary directory\n");
    return 1;
  }
  snprintf(target, sizeof target, "%s" SEP "target.txt", dir);
  snprintf(s.live, sizeof s.live, "s/self/live@%s", reported);
  snprintf(s.die, sizeof s.die, "s/self/die@%s", reported);
  snprintf(s.target, sizeof s.target, "%s" SEP "target.txt", reported);
  snprintf(renamed, sizeof renamed, "%s" SEP "renamed.txt", dir);
  snprintf(s.renamed, sizeof s.renamed, "%s" SEP "renamed.txt", reported);

  CHECK(!wtr_watcher_close(NULL));

  watcher = wtr_watcher_open(dir, on_event, &s);
  CHECK(watcher != NULL);
  if (watcher == NULL)
    return 1;

  /* There is no readiness signal, so create probe files until the watcher
     reports one. */
  for (i = 0; i < 40 && !ready; ++i) {
    snprintf(probe, sizeof probe, "%s" SEP "probe-%d", dir, i);
    CHECK(touch(probe));
    created = i + 1;
    ready = wait_flag(&s, &s.file_creates, 25);
  }
  CHECK(ready);

  CHECK(touch(target));
  CHECK(wait_flag(&s, &s.target_created, 1000));
  CHECK(rename(target, renamed) == 0);
  CHECK(wait_flag(&s, &s.rename_ok, 1000));

  /* Closing joins the watcher's thread: every callback has returned. */
  CHECK(wtr_watcher_close(watcher));
  CHECK(s.live_ok);
  CHECK(s.live_time > 1600000000000000000LL);
  CHECK(s.target_created);
  CHECK(s.rename_ok);
  CHECK(s.last_is_die);
  CHECK(s.events >= 5);

  remove(renamed);
  for (i = 0; i < created; ++i) {
    snprintf(probe, sizeof probe, "%s" SEP "probe-%d", dir, i);
    remove(probe);
  }
#if defined(_WIN32)
  RemoveDirectoryA(dir);
#else
  rmdir(dir);
#endif

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("watcher-c ok");
  return 0;
}
