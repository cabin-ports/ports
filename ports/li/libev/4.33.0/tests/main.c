/* Drives libev from C: the backends this build enabled, which only the
   HAVE_* macros in cabin.toml turn on; a default loop running timer,
   periodic, idle and async watchers; off Windows, I/O on a pipe through
   each compiled-in backend and signal and child watchers; and the libevent
   emulation in event.h. */
#ifdef _WIN32
#include <winsock2.h>
#ifdef _MSC_VER
/* event.h finds struct timeval through <winsock2.h> only with WIN32
   defined (cabin.toml). */
#ifndef WIN32
#define WIN32 1
#endif
#endif
#else
#define _POSIX_C_SOURCE 200112L
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(__linux__) && !defined(__ANDROID__)
#include <linux/version.h>
#include <sys/utsname.h>
#endif

#include <ev.h>
#include <event.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, int line, const char *what)
{
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

static void backends(void)
{
  unsigned int supported = ev_supported_backends();
  unsigned int baseline = EVBACKEND_POLL | EVBACKEND_SELECT;

  CHECK(ev_version_major() == EV_VERSION_MAJOR && ev_version_minor() == EV_VERSION_MINOR);
  CHECK(EV_VERSION_MAJOR == 4 && EV_VERSION_MINOR == 33);
  CHECK((ev_recommended_backends() & ~supported) == 0);
#if defined(__ANDROID__)
  /* ev.c disables select on Android. */
  CHECK(supported == EVBACKEND_POLL);
#elif defined(__linux__)
  CHECK((supported & (baseline | EVBACKEND_EPOLL)) == (baseline | EVBACKEND_EPOLL));
  CHECK(!(supported & (EVBACKEND_KQUEUE | EVBACKEND_LINUXAIO)));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
  {
    /* io_uring is compiled in, and supported from Linux 5.6.1 on. */
    struct utsname name;
    unsigned int major = 0, minor = 0, patch = 0;
    CHECK(uname(&name) == 0 && sscanf(name.release, "%u.%u.%u", &major, &minor, &patch) >= 2);
    if (((major << 16) | (minor << 8) | patch) >= 0x050601) CHECK(supported & EVBACKEND_IOURING);
  }
#endif
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
  CHECK((supported & (baseline | EVBACKEND_KQUEUE)) == (baseline | EVBACKEND_KQUEUE));
  CHECK(!(supported & EVBACKEND_EPOLL));
#elif defined(_WIN32)
  CHECK(supported == EVBACKEND_SELECT);
#endif
}

static int timer_fired, periodic_fired, idle_fired, async_fired;
static ev_tstamp periodic_at[2];
static ev_async wake;

static void timer_cb(struct ev_loop *loop, ev_timer *w, int revents)
{
  (void)w;
  CHECK(revents & EV_TIMER);
  ++timer_fired;
  ev_async_send(loop, &wake);
}

static void async_cb(struct ev_loop *loop, ev_async *w, int revents)
{
  CHECK(revents & EV_ASYNC);
  ++async_fired;
  ev_async_stop(loop, w);
}

static void periodic_cb(struct ev_loop *loop, ev_periodic *w, int revents)
{
  CHECK(revents & EV_PERIODIC);
  periodic_at[periodic_fired] = ev_periodic_at(w);
  if (++periodic_fired == 2) ev_periodic_stop(loop, w);
}

static void idle_cb(struct ev_loop *loop, ev_idle *w, int revents)
{
  CHECK(revents & EV_IDLE);
  ++idle_fired;
  ev_idle_stop(loop, w);
}

static void timers(struct ev_loop *loop)
{
  ev_timer timer;
  ev_periodic periodic;
  ev_idle idle;
  ev_tstamp start, steps, gap;

  ev_now_update(loop);
  start = ev_now(loop);
  ev_timer_init(&timer, timer_cb, 0.02, 0.);
  ev_periodic_init(&periodic, periodic_cb, 0., 0.01, 0);
  ev_idle_init(&idle, idle_cb);
  ev_async_init(&wake, async_cb);
  ev_timer_start(loop, &timer);
  ev_periodic_start(loop, &periodic);
  ev_idle_start(loop, &idle);
  ev_async_start(loop, &wake);

  /* Returns once every watcher has stopped: the timer after firing once,
     the others from their callbacks. */
  ev_run(loop, 0);
  CHECK(timer_fired == 1 && async_fired == 1 && idle_fired == 1 && periodic_fired == 2);
  CHECK(ev_now(loop) - start >= 0.019);
  /* A periodic with no offset is due at whole multiples of its interval;
     ev_periodic_at reports the next one, which skips any the loop woke
     too late for. */
  steps = periodic_at[0] / 0.01;
  CHECK(fabs(steps - floor(steps + 0.5)) < 1e-3);
  gap = (periodic_at[1] - periodic_at[0]) / 0.01;
  CHECK(gap > 0.5 && fabs(gap - floor(gap + 0.5)) < 1e-3);
}

#ifndef _WIN32
static int io_fired, signal_fired, child_fired;
static pid_t child;

static void io_cb(struct ev_loop *loop, ev_io *w, int revents)
{
  char c = 0;
  CHECK(revents & EV_READ);
  CHECK(read(w->fd, &c, 1) == 1 && c == 'x');
  ++io_fired;
  ev_io_stop(loop, w);
}

static void io_backend(unsigned int backend)
{
  struct ev_loop *loop;
  ev_io w;
  int fds[2];

  if (!(ev_supported_backends() & backend)) return;
  loop = ev_loop_new(backend | EVFLAG_NOENV);
  CHECK(loop != NULL);
  if (!loop) return;
  CHECK(ev_backend(loop) == backend);
  CHECK(pipe(fds) == 0);
  ev_io_init(&w, io_cb, fds[0], EV_READ);
  ev_io_start(loop, &w);
  CHECK(write(fds[1], "x", 1) == 1);
  io_fired = 0;
  ev_run(loop, 0);
  CHECK(io_fired == 1);
  close(fds[0]);
  close(fds[1]);
  ev_loop_destroy(loop);
}

static void signal_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
  CHECK(revents & EV_SIGNAL && w->signum == SIGUSR1);
  ++signal_fired;
  ev_signal_stop(loop, w);
}

static void child_cb(struct ev_loop *loop, ev_child *w, int revents)
{
  CHECK(revents & EV_CHILD);
  CHECK(w->rpid == child && WIFEXITED(w->rstatus) && WEXITSTATUS(w->rstatus) == 7);
  ++child_fired;
  ev_child_stop(loop, w);
}

static void posix(struct ev_loop *loop)
{
  ev_signal sig;
  ev_child kid;

  io_backend(EVBACKEND_EPOLL);
  io_backend(EVBACKEND_KQUEUE);
  io_backend(EVBACKEND_POLL);
  io_backend(EVBACKEND_SELECT);

  ev_signal_init(&sig, signal_cb, SIGUSR1);
  ev_signal_start(loop, &sig);
  CHECK(raise(SIGUSR1) == 0);
  ev_run(loop, 0);
  CHECK(signal_fired == 1);

  /* A child may exit before its watcher starts, as long as the loop has
     not run in between. */
  child = fork();
  if (child == 0) _exit(7);
  CHECK(child > 0);
  ev_child_init(&kid, child_cb, child, 0);
  ev_child_start(loop, &kid);
  ev_run(loop, 0);
  CHECK(child_fired == 1);
}
#endif

static int compat_fired, once_fired;

static void compat_timer_cb(int fd, short events, void *arg)
{
  CHECK(fd == -1 && events == EV_TIMEOUT && arg == &compat_fired);
  ++compat_fired;
}

static void compat_once_cb(int fd, short events, void *arg)
{
  CHECK(fd == -1 && events == EV_TIMEOUT && arg == &once_fired);
  ++once_fired;
}

#ifndef _WIN32
static void compat_io_cb(int fd, short events, void *arg)
{
  char c = 0;
  CHECK(events == EV_READ && arg == &compat_fired);
  CHECK(read(fd, &c, 1) == 1 && c == 'y');
  ++compat_fired;
}
#endif

static void compat(struct ev_loop *loop)
{
  struct event ev;
  struct timeval tv;

  /* The first event base is libev's default loop. */
  CHECK(event_init() == (void *)loop);
  CHECK(strcmp(event_get_version(), "4.33") == 0);
  CHECK(strcmp(event_get_method(), "libev") == 0);

  evtimer_set(&ev, compat_timer_cb, &compat_fired);
  tv.tv_sec = 0;
  tv.tv_usec = 10000;
  CHECK(evtimer_add(&ev, &tv) == 0);
  CHECK(evtimer_pending(&ev, NULL) == EV_TIMEOUT);
  CHECK(event_once(-1, EV_TIMEOUT, compat_once_cb, &once_fired, &tv) == 0);
  /* 1, as in libevent, once no events are left. */
  CHECK(event_dispatch() == 1);
  CHECK(compat_fired == 1 && once_fired == 1);
  CHECK(evtimer_pending(&ev, NULL) == 0);

#ifndef _WIN32
  {
    int fds[2];
    CHECK(pipe(fds) == 0);
    event_set(&ev, fds[0], EV_READ, compat_io_cb, &compat_fired);
    CHECK(event_add(&ev, NULL) == 0);
    CHECK(event_pending(&ev, EV_READ, NULL) == EV_READ);
    CHECK(write(fds[1], "y", 1) == 1);
    CHECK(event_loop(EVLOOP_ONCE) == 1 && compat_fired == 2);
    /* Without EV_PERSIST the event is deleted once it fires. */
    CHECK(event_pending(&ev, EV_READ, NULL) == 0);
    close(fds[0]);
    close(fds[1]);
  }
#endif
}

int main(void)
{
  struct ev_loop *loop;
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    fprintf(stderr, "WSAStartup failed\n");
    return 1;
  }
#endif

  backends();
  loop = ev_default_loop(EVFLAG_NOENV);
  CHECK(loop != NULL);
  if (!loop) return 1;
  /* The first recommended backend: epoll on Linux, select on macOS (which
     recommends neither kqueue nor poll) and Windows. */
#if defined(__ANDROID__)
  CHECK(ev_backend(loop) == EVBACKEND_POLL);
#elif defined(__linux__)
  CHECK(ev_backend(loop) == EVBACKEND_EPOLL);
#elif defined(__APPLE__) || defined(_WIN32)
  CHECK(ev_backend(loop) == EVBACKEND_SELECT);
#endif
  timers(loop);
#ifndef _WIN32
  posix(loop);
#endif
  compat(loop);

  printf("libev C: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
