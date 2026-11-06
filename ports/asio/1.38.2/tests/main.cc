#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int fail(const char* what) {
  std::fprintf(stderr, "asio: %s\n", what);
  return 1;
}

}  // namespace

int main() {
  static_assert(ASIO_VERSION == 103802, "asio version");

  // Timers: one expires, a cancelled one reports operation_aborted.
  {
    asio::io_context ctx;
    asio::steady_timer fires(ctx, std::chrono::milliseconds(10));
    asio::steady_timer cancelled(ctx, std::chrono::hours(1));
    bool fired = false;
    asio::error_code cancel_ec;
    fires.async_wait([&](const asio::error_code& ec) {
      fired = !ec;
      cancelled.cancel();
    });
    cancelled.async_wait([&](const asio::error_code& ec) { cancel_ec = ec; });
    ctx.run();
    if (!fired) return fail("timer did not fire");
    if (cancel_ec != asio::error::operation_aborted) return fail("timer cancel");
  }

  // A strand serializes handlers posted from several threads, so the
  // unsynchronized counter is still exact.
  {
    asio::io_context ctx;
    auto work = asio::make_work_guard(ctx);
    asio::strand<asio::io_context::executor_type> strand(ctx.get_executor());
    int counter = 0;
    std::atomic<int> done(0);
    std::vector<std::thread> runners;
    for (int i = 0; i < 4; ++i) runners.emplace_back([&ctx] { ctx.run(); });
    std::vector<std::thread> posters;
    for (int t = 0; t < 4; ++t)
      posters.emplace_back([&] {
        for (int i = 0; i < 1000; ++i)
          asio::post(strand, [&] {
            ++counter;
            if (++done == 4000) asio::post(ctx, [&] { ctx.stop(); });
          });
      });
    for (auto& p : posters) p.join();
    for (auto& r : runners) r.join();
    if (counter != 4000) return fail("strand counter");
  }

  // Asynchronous I/O through a pipe (POSIX pipes; named pipes with
  // overlapped I/O on Windows).
  {
    asio::io_context ctx;
    asio::readable_pipe reader(ctx);
    asio::writable_pipe writer(ctx);
    asio::connect_pipe(reader, writer);
    const std::string message(100000, 'x');
    std::string received(message.size(), '\0');
    std::size_t written = 0, read = 0;
    asio::error_code write_ec, read_ec;
    asio::async_write(writer, asio::buffer(message),
                      [&](const asio::error_code& ec, std::size_t n) {
                        write_ec = ec;
                        written = n;
                        writer.close();
                      });
    asio::async_read(reader, asio::buffer(&received[0], received.size()),
                     [&](const asio::error_code& ec, std::size_t n) {
                       read_ec = ec;
                       read = n;
                     });
    ctx.run();
    if (write_ec || written != message.size()) return fail("pipe write");
    if (read_ec || read != message.size() || received != message)
      return fail("pipe read");

    // With the write end closed, a further read reports end of file
    // (broken_pipe on Windows, where asio maps only ERROR_HANDLE_EOF to eof).
    char extra = 0;
    asio::error_code eof;
    reader.read_some(asio::buffer(&extra, 1), eof);
    if (eof != asio::error::eof && eof != asio::error::broken_pipe)
      return fail("pipe end of file");
  }
  return 0;
}
