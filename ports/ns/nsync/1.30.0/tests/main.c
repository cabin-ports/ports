#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "nsync.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,     \
              #cond);                                                      \
      exit(1);                                                             \
    }                                                                      \
  } while (0)

#define THREADS 4
#define ITERATIONS 10000

struct shared {
  nsync_mu mu;
  nsync_cv cv;
  int total;
  int ready;
  nsync_counter done;
};

static void worker(struct shared *s) {
  int i;
  for (i = 0; i != ITERATIONS; i++) {
    nsync_mu_lock(&s->mu);
    s->total++;
    nsync_mu_unlock(&s->mu);
  }
  nsync_counter_add(s->done, -1);
}

static void signaller(struct shared *s) {
  nsync_time_sleep(nsync_time_ms(20));
  nsync_mu_lock(&s->mu);
  s->ready = 1;
  nsync_cv_signal(&s->cv);
  nsync_mu_unlock(&s->mu);
}

#if defined(_WIN32)
typedef HANDLE thread;
static DWORD WINAPI worker_main(LPVOID arg) {
  worker((struct shared *)arg);
  return 0;
}
static DWORD WINAPI signaller_main(LPVOID arg) {
  signaller((struct shared *)arg);
  return 0;
}
static thread start(int signals, struct shared *s) {
  thread t =
      CreateThread(NULL, 0, signals ? signaller_main : worker_main, s, 0, NULL);
  CHECK(t != NULL);
  return t;
}
static void join(thread t) {
  CHECK(WaitForSingleObject(t, INFINITE) == WAIT_OBJECT_0);
  CHECK(CloseHandle(t));
}
#else
typedef pthread_t thread;
static void *worker_main(void *arg) {
  worker((struct shared *)arg);
  return NULL;
}
static void *signaller_main(void *arg) {
  signaller((struct shared *)arg);
  return NULL;
}
static thread start(int signals, struct shared *s) {
  thread t;
  CHECK(pthread_create(&t, NULL, signals ? signaller_main : worker_main, s) ==
        0);
  return t;
}
static void join(thread t) { CHECK(pthread_join(t, NULL) == 0); }
#endif

static int total_reached(const void *v) {
  return ((const struct shared *)v)->total == THREADS * ITERATIONS;
}

/* Deadlines for waits that must succeed are 10 s away, so a broken wakeup
   fails the test instead of hanging it. */
static nsync_time in_ms(int ms) {
  return nsync_time_add(nsync_time_now(), nsync_time_ms(ms));
}

/* Whether the deadline has passed, allowing the 1 ms early wakeup that
   upstream's testing/cv_test.c tolerates. */
static int reached(nsync_time deadline) {
  return nsync_time_cmp(nsync_time_now(),
                        nsync_time_sub(deadline, nsync_time_ms(1))) >= 0;
}

static void test_time(void) {
  CHECK(nsync_time_cmp(nsync_time_ms(1500), nsync_time_s_ns(1, 500000000)) ==
        0);
  CHECK(nsync_time_cmp(nsync_time_us(2500), nsync_time_s_ns(0, 2500000)) == 0);
  CHECK(nsync_time_cmp(nsync_time_add(nsync_time_ms(1500),
                                      nsync_time_us(700000)),
                       nsync_time_s_ns(2, 200000000)) == 0);
  CHECK(nsync_time_cmp(nsync_time_sub(nsync_time_s_ns(3, 0), nsync_time_ms(1)),
                       nsync_time_s_ns(2, 999000000)) == 0);
  CHECK(nsync_time_cmp(nsync_time_zero, nsync_time_s_ns(0, 0)) == 0);
  CHECK(nsync_time_cmp(nsync_time_ms(999), nsync_time_s_ns(1, 0)) < 0);
  CHECK(nsync_time_cmp(nsync_time_s_ns(2, 1), nsync_time_s_ns(2, 0)) > 0);
  CHECK(nsync_time_cmp(nsync_time_no_deadline, nsync_time_s_ns(1L << 30, 0)) >
        0);

  CHECK(nsync_time_cmp(nsync_time_sleep(nsync_time_ms(10)), nsync_time_zero) ==
        0);
}

static void test_mu(void) {
  nsync_mu mu;
  nsync_mu_init(&mu);
  CHECK(nsync_mu_trylock(&mu) != 0);
  CHECK(nsync_mu_is_reader(&mu) == 0);
  CHECK(nsync_mu_trylock(&mu) == 0);
  CHECK(nsync_mu_rtrylock(&mu) == 0);
  nsync_mu_unlock(&mu);
  CHECK(nsync_mu_rtrylock(&mu) != 0);
  CHECK(nsync_mu_rtrylock(&mu) != 0);
  CHECK(nsync_mu_is_reader(&mu) != 0);
  CHECK(nsync_mu_trylock(&mu) == 0);
  nsync_mu_runlock(&mu);
  CHECK(nsync_mu_trylock(&mu) == 0);
  nsync_mu_runlock(&mu);
  CHECK(nsync_mu_trylock(&mu) != 0);
  nsync_mu_unlock(&mu);
}

static void test_threads(void) {
  struct shared s;
  thread threads[THREADS];
  thread t;
  nsync_time deadline;
  int i;

  nsync_mu_init(&s.mu);
  nsync_cv_init(&s.cv);
  s.total = 0;
  s.ready = 0;
  s.done = nsync_counter_new(THREADS);
  CHECK(s.done != NULL);
  CHECK(nsync_counter_value(s.done) == THREADS);

  for (i = 0; i != THREADS; i++) {
    threads[i] = start(0, &s);
  }
  nsync_mu_lock(&s.mu);
  CHECK(nsync_mu_wait_with_deadline(&s.mu, &total_reached, &s, NULL,
                                    in_ms(10000), NULL) == 0);
  CHECK(s.total == THREADS * ITERATIONS);
  nsync_mu_unlock(&s.mu);
  CHECK(nsync_counter_wait(s.done, in_ms(10000)) == 0);
  CHECK(nsync_counter_value(s.done) == 0);
  for (i = 0; i != THREADS; i++) {
    join(threads[i]);
  }
  nsync_counter_free(s.done);

  /* A wait that times out returns ETIMEDOUT, no earlier than its deadline,
     with the mutex held again. */
  nsync_mu_lock(&s.mu);
  deadline = in_ms(20);
  CHECK(nsync_cv_wait_with_deadline(&s.cv, &s.mu, deadline, NULL) ==
        ETIMEDOUT);
  CHECK(reached(deadline));
  CHECK(nsync_mu_trylock(&s.mu) == 0);

  /* Another thread's signal ends the wait. */
  t = start(1, &s);
  while (!s.ready) {
    CHECK(nsync_cv_wait_with_deadline(&s.cv, &s.mu, in_ms(10000), NULL) == 0);
  }
  nsync_mu_unlock(&s.mu);
  join(t);
}

static void test_notes_and_waiters(void) {
  nsync_note parent;
  nsync_note child;
  nsync_note timed;
  nsync_counter counter;
  nsync_time deadline;
  struct nsync_waitable_s w[2];
  struct nsync_waitable_s *pw[2];

  parent = nsync_note_new(NULL, nsync_time_no_deadline);
  child = nsync_note_new(parent, nsync_time_no_deadline);
  CHECK(parent != NULL && child != NULL);
  CHECK(nsync_note_is_notified(parent) == 0);
  CHECK(nsync_note_is_notified(child) == 0);
  CHECK(nsync_note_wait(child, nsync_time_zero) == 0);
  nsync_note_notify(parent);
  CHECK(nsync_note_is_notified(child) != 0);
  CHECK(nsync_note_wait(child, in_ms(10000)) != 0);

  deadline = in_ms(20);
  timed = nsync_note_new(NULL, deadline);
  CHECK(timed != NULL);
  CHECK(nsync_time_cmp(nsync_note_expiry(timed), deadline) == 0);
  CHECK(nsync_note_wait(timed, in_ms(10000)) != 0);
  CHECK(reached(deadline));
  nsync_note_free(timed);

  /* nsync_wait_n returns the index of the notified waitable, or the count
     when the deadline passes first. */
  nsync_note_free(child);
  child = nsync_note_new(NULL, nsync_time_no_deadline);
  counter = nsync_counter_new(1);
  CHECK(child != NULL && counter != NULL);
  w[0].v = child;
  w[0].funcs = &nsync_note_waitable_funcs;
  w[1].v = counter;
  w[1].funcs = &nsync_counter_waitable_funcs;
  pw[0] = &w[0];
  pw[1] = &w[1];
  deadline = in_ms(20);
  CHECK(nsync_wait_n(NULL, NULL, NULL, deadline, 2, pw) == 2);
  CHECK(reached(deadline));
  CHECK(nsync_counter_add(counter, -1) == 0);
  CHECK(nsync_wait_n(NULL, NULL, NULL, in_ms(10000), 2, pw) == 1);
  nsync_counter_free(counter);

  /* A note that expires while the call waits. */
  deadline = in_ms(20);
  timed = nsync_note_new(NULL, deadline);
  CHECK(timed != NULL);
  w[1].v = timed;
  w[1].funcs = &nsync_note_waitable_funcs;
  CHECK(nsync_wait_n(NULL, NULL, NULL, in_ms(10000), 2, pw) == 1);
  CHECK(reached(deadline));
  nsync_note_free(timed);

  nsync_note_notify(child);
  CHECK(nsync_wait_n(NULL, NULL, NULL, in_ms(10000), 1, pw) == 0);
  nsync_note_free(child);
  nsync_note_free(parent);
}

static int once_calls;
static void once_body(void) { once_calls++; }
static void once_arg_body(void *arg) { *(int *)arg += 10; }

static void test_once(void) {
  static nsync_once once = NSYNC_ONCE_INIT;
  static nsync_once once_arg = NSYNC_ONCE_INIT;
  int value = 1;
  nsync_run_once(&once, &once_body);
  nsync_run_once(&once, &once_body);
  CHECK(once_calls == 1);
  nsync_run_once_arg(&once_arg, &once_arg_body, &value);
  nsync_run_once_arg(&once_arg, &once_arg_body, &value);
  CHECK(value == 11);
}

int main(void) {
  test_time();
  test_mu();
  test_threads();
  test_notes_and_waiters();
  test_once();
  puts("nsync ok");
  return 0;
}
