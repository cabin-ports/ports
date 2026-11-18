#include <transwarp.h>

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

namespace tw = transwarp;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// a ---+--> sum (consume) --> squared (then, consume) --+
//      |                                                 +--> result
// b ---+--> label (accept) ------------------------------+    (consume)
//      sum, label --> done (wait) ----------------------+
void TestGraph(tw::executor& executor) {
  int x = 3;
  std::atomic<int> waits{0};
  auto a = tw::make_task(tw::root, [&x] { return x * 2; });
  auto b = tw::make_task(tw::root, [] { return std::string("ab"); });
  auto sum = tw::make_task(
      tw::consume,
      [](int v, std::string const& s) {
        return v + static_cast<int>(s.size());
      },
      a, b);
  auto squared = sum->then(tw::consume, [](int v) { return v * v; });
  auto label = tw::make_task(
      tw::accept,
      [](auto fa, auto fb) { return fb.get() + std::to_string(fa.get()); }, a,
      b);
  auto done = tw::make_task(tw::wait, [&waits] { return ++waits; }, sum, label);
  auto result = tw::make_task(
      tw::consume,
      [](int sq, std::string const& l, int w) {
        return l + ":" + std::to_string(sq) + ":" + std::to_string(w);
      },
      squared, label, done);

  result->schedule_all(executor);
  check(result->get() == "ab6:64:1", "first run");
  x = 10;
  result->schedule_all(executor);
  check(result->get() == "ab20:484:2", "rerun sees the new input");
  check(sum->get() == 22 && waits == 2, "intermediate results");
}

void TestException(tw::executor& executor) {
  std::atomic<int> consumed{0};
  auto bad = tw::make_task(tw::root, []() -> int {
    throw std::runtime_error("boom");
  });
  auto good = tw::make_value_task(5);
  auto child = tw::make_task(
      tw::consume,
      [&consumed](int v, int w) {
        ++consumed;
        return v + w;
      },
      bad, good);
  auto handler = tw::make_task(
      tw::accept,
      [](auto fbad, auto fgood) -> std::string {
        try {
          return std::to_string(fbad.get());
        } catch (std::runtime_error const& e) {
          return std::string("caught ") + e.what() + " " +
                 std::to_string(fgood.get());
        }
      },
      bad, good);
  child->schedule_all(executor);
  std::string message;
  try {
    child->get();
  } catch (std::runtime_error const& e) {
    message = e.what();
  }
  check(message == "boom" && consumed == 0,
        "an exception skips consume children and reaches get()");

  // Scheduling handler reschedules the shared parents, so child's run must
  // have finished first.
  handler->schedule_all(executor);
  check(handler->get() == "caught boom 5", "accept sees the failed future");
}

void TestThreads() {
  auto const main_id = std::this_thread::get_id();
  auto where = [] { return std::this_thread::get_id(); };

  tw::sequential sequential;
  auto here = tw::make_task(tw::root, where);
  here->schedule(sequential);
  check(here->get() == main_id, "sequential runs on the caller");

  tw::parallel parallel{2};
  auto there = tw::make_task(tw::root, where);
  there->schedule(parallel);
  check(there->get() != main_id, "parallel runs on a pool thread");
}

}  // namespace

int main() {
  tw::sequential sequential;
  tw::parallel parallel{4};
  TestGraph(sequential);
  TestGraph(parallel);
  TestException(sequential);
  TestException(parallel);
  TestThreads();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("transwarp consumer test passed");
  return 0;
}
