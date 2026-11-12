// Drives libev through ev++.h: member-function and free-function callbacks
// on timer, async and idle watchers of the default loop, another thread
// waking that loop through its async watcher, the ev::bad_loop a loop
// asking only for a backend this build lacks throws, and, off Windows, I/O
// on a pipe through a poll loop.
#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

#include <ev++.h>

#include <chrono>
#include <cstdio>
#include <thread>

namespace {

int failures = 0;

void check(bool ok, int line, const char *what)
{
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

struct Counter {
  ev::async *wake;
  int timer;
  int async;

  Counter() : wake(0), timer(0), async(0) {}

  void on_timer(ev::timer &w, int revents)
  {
    CHECK((revents & ev::TIMER) && !w.is_active());
    ++timer;
    wake->send();
  }

  void on_async(ev::async &w, int revents)
  {
    CHECK(revents & ev::ASYNC);
    ++async;
    w.stop();
  }
};

// Woken from another thread while the loop waits in its backend, which
// crosses libev's eventfd, pipe or, on Windows, socket pair; the guard
// timer ends the loop if the wakeup never arrives.
struct CrossWake {
  ev::async async;
  ev::timer guard;
  int woken;
  bool timed_out;

  explicit CrossWake(ev::loop_ref loop) : async(loop), guard(loop), woken(0), timed_out(false)
  {
    async.set<CrossWake, &CrossWake::on_async>(this);
    guard.set<CrossWake, &CrossWake::on_guard>(this);
  }

  void on_async(ev::async &, int)
  {
    ++woken;
    async.stop();
    guard.stop();
  }

  void on_guard(ev::timer &, int)
  {
    timed_out = true;
    async.stop();
  }
};

int idle_fired = 0;

void on_idle(ev::idle &w, int revents)
{
  CHECK(revents & ev::IDLE);
  ++idle_fired;
  w.stop();
}

#ifndef _WIN32
int io_fired = 0;

void on_io(ev::io &w, int revents)
{
  char c = 0;
  CHECK(revents & ev::READ);
  CHECK(read(w.fd, &c, 1) == 1 && c == 'z');
  ++io_fired;
  w.stop();
}
#endif

}  // namespace

int main()
{
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::fprintf(stderr, "WSAStartup failed\n");
    return 1;
  }
#endif

  CHECK(ev::version_major() == 4 && ev::version_minor() == 33);
  CHECK(ev::recommended_backends() != 0 &&
        (ev::recommended_backends() & ~ev::supported_backends()) == 0);

  ev::default_loop loop(EVFLAG_NOENV);
  Counter counter;
  ev::timer timer(loop);
  ev::async wake(loop);
  ev::idle idle(loop);
  counter.wake = &wake;
  timer.set<Counter, &Counter::on_timer>(&counter);
  wake.set<Counter, &Counter::on_async>(&counter);
  idle.set<on_idle>();
  wake.start();
  idle.start();
  timer.start(0.01, 0.);
  loop.run();
  CHECK(counter.timer == 1 && counter.async == 1 && idle_fired == 1);

  CrossWake cross(loop);
  cross.async.start();
  cross.guard.start(30., 0.);
  std::thread sender([&cross] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cross.async.send();
  });
  loop.run();
  sender.join();
  CHECK(cross.woken == 1 && !cross.timed_out);

  // No build of libev for these systems has Solaris's event ports.
  bool threw = false;
  try {
    ev::dynamic_loop ports(ev::PORT | EVFLAG_NOENV);
  } catch (const ev::bad_loop &) {
    threw = true;
  }
  CHECK(threw);

#ifndef _WIN32
  ev::dynamic_loop polling(ev::POLL | EVFLAG_NOENV);
  CHECK(polling.backend() == ev::POLL);
  int fds[2];
  CHECK(pipe(fds) == 0);
  ev::io io(polling);
  io.set<on_io>();
  io.start(fds[0], ev::READ);
  CHECK(write(fds[1], "z", 1) == 1);
  polling.run();
  CHECK(io_fired == 1);
  close(fds[0]);
  close(fds[1]);
#endif

  std::printf("libev C++: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
