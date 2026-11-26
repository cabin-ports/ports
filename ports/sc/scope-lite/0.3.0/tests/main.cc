// Consumer test from C++98: scope_exit, scope_fail and scope_success on
// normal exit and on an exception, release(), and unique_resource's single
// call of its deleter (destruction, reset, release, a checked factory).
#include <nonstd/scope.hpp>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

int exits = 0, fails = 0, successes = 0;
void on_exit() { ++exits; }
void on_fail() { ++fails; }
void on_success() { ++successes; }

int closed = 0, last_closed = 0;
void close_handle(int handle) {
  ++closed;
  last_closed = handle;
}

typedef void (*Action)();
typedef void (*Deleter)(int);

void reset_counters() {
  exits = fails = successes = 0;
  closed = last_closed = 0;
}

// Runs all three guards, then throws when asked.
void guarded(bool do_throw) {
  nonstd::scope_exit<Action> e(&on_exit);
  nonstd::scope_fail<Action> f(&on_fail);
  nonstd::scope_success<Action> s(&on_success);
  if (do_throw) throw 1;
}

void released(bool do_throw) {
  nonstd::scope_exit<Action> e(&on_exit);
  nonstd::scope_fail<Action> f(&on_fail);
  nonstd::scope_success<Action> s(&on_success);
  e.release();
  f.release();
  s.release();
  if (do_throw) throw 1;
}

}  // namespace

int main() {
  // Normal exit: scope_exit and scope_success run once, scope_fail never.
  guarded(false);
  CHECK(exits == 1 && fails == 0 && successes == 1);

  // Exception: scope_exit and scope_fail run once, scope_success never.
  reset_counters();
  bool caught = false;
  try {
    guarded(true);
  } catch (int) {
    caught = true;
  }
  CHECK(caught && exits == 1 && fails == 1 && successes == 0);

  // release() disarms a guard on either path.
  reset_counters();
  released(false);
  try {
    released(true);
  } catch (int) {
  }
  CHECK(exits == 0 && fails == 0 && successes == 0);

  // A guard from the factory runs at the end of its scope, not before.
  reset_counters();
  {
    nonstd::scope_exit<Action> g = nonstd::make_scope_exit(&on_exit);
    CHECK(exits == 0);
    (void)g;
  }
  CHECK(exits == 1);

  // unique_resource: the deleter runs once, with the handle, on destruction.
  reset_counters();
  {
    nonstd::unique_resource<int, Deleter> r(7, &close_handle);
    CHECK(r.get() == 7 && closed == 0);
  }
  CHECK(closed == 1 && last_closed == 7);

  // reset() releases the old handle at once and adopts the new one.
  reset_counters();
  {
    nonstd::unique_resource<int, Deleter> r(1, &close_handle);
    r.reset(2);
    CHECK(closed == 1 && last_closed == 1 && r.get() == 2);
    r.reset();
    CHECK(closed == 2 && last_closed == 2);
  }
  CHECK(closed == 2);  // nothing left to delete

  // release() gives up ownership: the deleter never runs.
  reset_counters();
  {
    nonstd::unique_resource<int, Deleter> r(5, &close_handle);
    r.release();
  }
  CHECK(closed == 0);

  // make_unique_resource_checked deletes a valid handle, not the invalid one.
  reset_counters();
  {
    nonstd::unique_resource<int, Deleter> ok =
        nonstd::make_unique_resource_checked(9, -1, &close_handle);
    CHECK(ok.get() == 9);
  }
  CHECK(closed == 1 && last_closed == 9);

  reset_counters();
  {
    nonstd::unique_resource<int, Deleter> bad =
        nonstd::make_unique_resource_checked(-1, -1, &close_handle);
    CHECK(bad.get() == -1);
  }
  CHECK(closed == 0);

  return failures == 0 ? 0 : 1;
}
