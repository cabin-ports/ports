#include <stdlib.h>
#include <stdio.h>
#include <tinycthread.h>

#define NTHREADS 4
#define NITER 1000

static int failures;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("FAIL line %d: %s\n", __LINE__, #c); \
      failures++; \
    } \
  } while (0)

static mtx_t m;
static mtx_t tm;
static cnd_t cv;
static tss_t key;
static int counter;
static int dtor_count;
static int init_count;
static int held;
static int release;

static void dtor(void *p) {
  free(p);
  CHECK(mtx_lock(&m) == thrd_success);
  dtor_count++;
  mtx_unlock(&m);
}

static int worker(void *arg) {
  int i;
  for (i = 0; i < NITER; i++) {
    CHECK(mtx_lock(&m) == thrd_success);
    counter++;
    mtx_unlock(&m);
  }
  tss_set(key, malloc(1));
  return 10 * (int)(size_t)arg;
}

static void init(void) { init_count++; }

/* Holds the timed mutex tm until main releases it. */
static int holder(void *arg) {
  (void)arg;
  CHECK(mtx_lock(&tm) == thrd_success);
  CHECK(mtx_lock(&m) == thrd_success);
  held = 1;
  CHECK(cnd_broadcast(&cv) == thrd_success);
  while (!release) CHECK(cnd_wait(&cv, &m) == thrd_success);
  mtx_unlock(&m);
  mtx_unlock(&tm);
  return 0;
}

int main(void) {
  thrd_t t[NTHREADS];
  thrd_t h;
  once_flag once = ONCE_FLAG_INIT;
  struct timespec ts, d;
  int i, res;

  CHECK(TINYCTHREAD_VERSION == 102);
  CHECK(mtx_init(&m, mtx_plain) == thrd_success);
  CHECK(mtx_init(&tm, mtx_timed) == thrd_success);
  CHECK(cnd_init(&cv) == thrd_success);
  CHECK(tss_create(&key, dtor) == thrd_success);

  for (i = 0; i < NTHREADS; i++)
    CHECK(thrd_create(&t[i], worker, (void *)(size_t)i) == thrd_success);
  for (i = 0; i < NTHREADS; i++) {
    res = -1;
    CHECK(thrd_join(t[i], &res) == thrd_success);
    CHECK(res == 10 * i);
  }
  CHECK(counter == NTHREADS * NITER);
  CHECK(dtor_count == NTHREADS);

  call_once(&once, init);
  call_once(&once, init);
  CHECK(init_count == 1);

  CHECK(thrd_create(&h, holder, NULL) == thrd_success);
  CHECK(mtx_lock(&m) == thrd_success);
  while (!held) CHECK(cnd_wait(&cv, &m) == thrd_success);
  mtx_unlock(&m);
  CHECK(mtx_trylock(&tm) == thrd_busy);
  CHECK(timespec_get(&ts, TIME_UTC) == TIME_UTC);
  ts.tv_nsec += 50 * 1000000;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }
  CHECK(mtx_timedlock(&tm, &ts) == thrd_timedout);
  CHECK(mtx_lock(&m) == thrd_success);
  release = 1;
  CHECK(cnd_broadcast(&cv) == thrd_success);
  mtx_unlock(&m);
  CHECK(thrd_join(h, &res) == thrd_success);
  CHECK(res == 0);
  CHECK(mtx_trylock(&tm) == thrd_success);
  mtx_unlock(&tm);

  d.tv_sec = 0;
  d.tv_nsec = 1000000;
  CHECK(thrd_sleep(&d, NULL) == 0);
  CHECK(thrd_equal(thrd_current(), thrd_current()));

  mtx_destroy(&tm);
  mtx_destroy(&m);
  cnd_destroy(&cv);
  tss_delete(key);
  return failures != 0;
}
