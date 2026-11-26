#include <cppcoro/async_manual_reset_event.hpp>
#include <cppcoro/cancellation_registration.hpp>
#include <cppcoro/cancellation_source.hpp>
#include <cppcoro/generator.hpp>
#include <cppcoro/net/ipv4_address.hpp>
#include <cppcoro/net/ipv6_address.hpp>
#include <cppcoro/static_thread_pool.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <cppcoro/when_all.hpp>

#if defined(_WIN32) || defined(__linux__)
#define HAVE_IO_LAYER 1
#include <cppcoro/io_service.hpp>
#include <cppcoro/read_write_file.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#endif

#include <cstdio>
#include <thread>
#include <tuple>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

cppcoro::task<int> square(int x) { co_return x * x; }

cppcoro::task<int> sum_of_squares() {
  co_return co_await square(3) + co_await square(4);
}

cppcoro::generator<int> count_to(int n) {
  for (int i = 1; i <= n; ++i) {
    co_yield i;
  }
}

cppcoro::task<bool> runs_off_thread(cppcoro::static_thread_pool &pool,
                                    std::thread::id main_id) {
  co_await pool.schedule();
  co_return std::this_thread::get_id() != main_id;
}

cppcoro::task<int> wait_then_value(cppcoro::async_manual_reset_event &event,
                                   int value) {
  co_await event;
  co_return value;
}

cppcoro::task<int> set_event(cppcoro::async_manual_reset_event &event) {
  event.set();
  co_return 0;
}

#ifdef HAVE_IO_LAYER
// Writes 100 bytes through the I/O layer and reads the two halves back;
// returns the bytes read, or a negative value on a mismatch.
cppcoro::task<int> file_round_trip(cppcoro::io_service &ios,
                                   const std::filesystem::path &path) {
  cppcoro::io_work_scope scope{ios};
  auto file = cppcoro::read_write_file::open(ios, path);
  char out[100];
  for (int i = 0; i < 100; ++i) {
    out[i] = static_cast<char>('a' + i % 26);
  }
  co_await file.write(0, out, sizeof out);
  char in[100];
  std::memset(in, 0, sizeof in);
  const auto first = co_await file.read(0, in, 50);
  const auto second = co_await file.read(50, in + 50, 50);
  if (first != 50 || second != 50 || std::memcmp(in, out, sizeof out) != 0 ||
      file.size() != 100) {
    co_return -1;
  }
  co_return static_cast<int>(first + second);
}

cppcoro::task<int> stop_after(cppcoro::io_service &ios,
                              cppcoro::task<int> job) {
  const int result = co_await std::move(job);
  ios.stop();
  co_return result;
}

cppcoro::task<int> process(cppcoro::io_service &ios) {
  ios.process_events();
  co_return 0;
}
#endif

} // namespace

int main() {
  CHECK(cppcoro::sync_wait(sum_of_squares()) == 25);

  int total = 0;
  for (int v : count_to(5)) {
    total += v;
  }
  CHECK(total == 15);

  auto [a, b] = cppcoro::sync_wait(
      cppcoro::when_all(square(2), square(5)));
  CHECK(a == 4 && b == 25);

  cppcoro::async_manual_reset_event event;
  auto [waited, setter] = cppcoro::sync_wait(
      cppcoro::when_all(wait_then_value(event, 42), set_event(event)));
  CHECK(waited == 42 && setter == 0);

  {
    cppcoro::static_thread_pool pool{2};
    CHECK(pool.thread_count() == 2);
    CHECK(cppcoro::sync_wait(runs_off_thread(pool, std::this_thread::get_id())));
  }

  cppcoro::cancellation_source source;
  auto token = source.token();
  int fired = 0;
  cppcoro::cancellation_registration registration{token, [&] { ++fired; }};
  CHECK(!token.is_cancellation_requested() && fired == 0);
  source.request_cancellation();
  CHECK(token.is_cancellation_requested() && fired == 1);

  using cppcoro::net::ipv4_address;
  auto v4 = ipv4_address::from_string("192.168.0.1");
  CHECK(v4.has_value() && v4->to_string() == "192.168.0.1");
  CHECK(v4 && *v4 == ipv4_address{0xC0A80001u} && !v4->is_loopback());
  CHECK(!ipv4_address::from_string("256.1.1.1").has_value());
  CHECK(ipv4_address::loopback().to_string() == "127.0.0.1");

  using cppcoro::net::ipv6_address;
  auto v6 = ipv6_address::from_string("2001:db8::1");
  CHECK(v6.has_value() && v6->to_string() == "2001:db8::1");
  CHECK(ipv6_address::loopback().to_string() == "::1");
  CHECK(!ipv6_address::from_string("2001:db8::g").has_value());

#ifdef HAVE_IO_LAYER
  const auto path =
      std::filesystem::temp_directory_path() /
      ("cppcoro-consumer-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".bin");
  {
    cppcoro::io_service ios;
    auto results = cppcoro::sync_wait(cppcoro::when_all(
        stop_after(ios, file_round_trip(ios, path)), process(ios)));
    CHECK(std::get<0>(results) == 100);
  }
  std::filesystem::remove(path);
#endif

  if (failures != 0) {
    return 1;
  }
  std::puts("ok");
  return 0;
}
