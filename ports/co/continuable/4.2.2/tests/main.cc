#include <continuable/continuable.hpp>

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

static int failures = 0;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::printf("FAIL line %d: %s\n", __LINE__, #c);                         \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main() {
  // then chain
  int chained = 0;
  cti::make_ready_continuable(20)
      .then([](int x) { return x + 1; })
      .then([&](int x) { chained = x * 2; });
  CHECK(chained == 42);

  // when_all combines results
  int all = 0;
  cti::when_all(cti::make_ready_continuable(1), cti::make_ready_continuable(2),
                cti::make_ready_continuable(4))
      .then([&](int a, int b, int c) { all = a * 100 + b * 10 + c; });
  CHECK(all == 124);

  // when_seq combines results in order
  std::string seq;
  cti::when_seq(cti::make_ready_continuable(std::string("a")),
                cti::make_ready_continuable(std::string("b")))
      .then([&](std::string a, std::string b) { seq = b + a; });
  CHECK(seq == "ba");

  // exceptional path reaches fail(), and skips then()
  bool thenRan = false, failed = false;
  std::string what;
  cti::make_exceptional_continuable<int>(
      std::make_exception_ptr(std::runtime_error("boom")))
      .then([&](int) { thenRan = true; })
      .fail([&](cti::exception_t e) {
        failed = true;
        try {
          std::rethrow_exception(e);
        } catch (std::runtime_error const& ex) {
          what = ex.what();
        }
      });
  CHECK(failed && !thenRan && what == "boom");

  // an exception thrown by a handler also reaches fail()
  bool thrown = false;
  cti::make_ready_continuable(1)
      .then([](int) -> int { throw std::runtime_error("x"); })
      .fail([&](cti::exception_t) { thrown = true; });
  CHECK(thrown);

  // promise resolved later by hand
  cti::promise<int> promise;
  int got = 0;
  cti::make_continuable<int>([&](auto&& p) { promise = std::forward<decltype(p)>(p); })
      .then([&](int x) { got = x; });
  CHECK(got == 0);
  promise.set_value(7);
  CHECK(got == 7);

  // promise rejected later by hand
  cti::promise<int> rejected;
  bool rejectedSeen = false;
  cti::make_continuable<int>([&](auto&& p) { rejected = std::forward<decltype(p)>(p); })
      .fail([&](cti::exception_t) { rejectedSeen = true; });
  CHECK(!rejectedSeen);
  rejected.set_exception(std::make_exception_ptr(std::runtime_error("late")));
  CHECK(rejectedSeen);

  return failures == 0 ? 0 : 1;
}
