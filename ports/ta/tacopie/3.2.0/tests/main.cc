// No TCP connection is made, so the test needs no network (on Windows the
// io_service self-pipe is a loopback UDP socket): the thread pool, the
// error type and the default state of the TCP classes run everywhere, and on
// Unix an io_service polls a pipe wrapped as a tcp_socket and fires its read
// callback.
#include <tacopie/tacopie>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

}  // namespace

int main() {
#ifdef _WIN32
  WSADATA data;
  CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
#endif

  // thread_pool runs every task, grows and shrinks, and stops.
  {
    tacopie::utils::thread_pool pool(2);
    pool.set_nb_threads(4);
    CHECK(pool.is_running());

    std::atomic<int> sum(0);
    std::mutex m;
    std::condition_variable cv;
    int finished = 0;
    for (int i = 1; i <= 10; ++i) {
      pool << [&, i] {
        sum += i;
        std::lock_guard<std::mutex> lock(m);
        ++finished;
        cv.notify_one();
      };
    }
    {
      std::unique_lock<std::mutex> lock(m);
      CHECK(cv.wait_for(lock, std::chrono::seconds(10), [&] { return finished == 10; }));
    }
    CHECK(sum == 55);

    pool.add_task([] { throw std::runtime_error("swallowed by the pool"); });
    pool.add_task([&] {
      std::lock_guard<std::mutex> lock(m);
      finished = 11;
      cv.notify_one();
    });
    {
      std::unique_lock<std::mutex> lock(m);
      CHECK(cv.wait_for(lock, std::chrono::seconds(10), [&] { return finished == 11; }));
    }

    pool.stop();
    CHECK(!pool.is_running());
  }

  // tacopie_error keeps its message, file and line.
  {
    bool caught = false;
    try {
      throw tacopie::tacopie_error("boom", "file.cc", 42);
    } catch (const tacopie::tacopie_error& e) {
      caught = std::string(e.what()) == "boom" && e.get_file() == "file.cc" &&
               e.get_line() == 42;
    }
    CHECK(caught);
  }

  // Objects start out idle and unconnected.
  {
    tacopie::tcp_socket socket;
    CHECK(socket.get_fd() == __TACOPIE_INVALID_FD);
    CHECK(socket.get_type() == tacopie::tcp_socket::type::UNKNOWN);

    tacopie::tcp_client client;
    CHECK(!client.is_connected());
    tacopie::tcp_server server;
    CHECK(!server.is_running());
  }

  // io_service construction starts and stops its poll thread (on Windows,
  // its self-pipe needs the WSAStartup above).
  {
    auto service = std::make_shared<tacopie::io_service>(1);
    service->set_nb_workers(2);
  }

#ifndef _WIN32
  {
    int fds[2];
    CHECK(pipe(fds) == 0);
    tacopie::tcp_socket socket(fds[0], "pipe", 7, tacopie::tcp_socket::type::CLIENT);
    CHECK(socket.get_host() == "pipe");
    CHECK(socket.get_port() == 7);
    CHECK(socket.get_type() == tacopie::tcp_socket::type::CLIENT);
    CHECK(socket.get_fd() == fds[0]);

    tacopie::io_service service(1);
    std::mutex m;
    std::condition_variable cv;
    int fired_fd = -1;
    char received = 0;
    service.track(socket, [&](tacopie::fd_t fd) {
      char c = 0;
      ssize_t n = read(fd, &c, 1);
      std::lock_guard<std::mutex> lock(m);
      if (n == 1) {
        fired_fd = fd;
        received = c;
        cv.notify_one();
      }
    });

    CHECK(write(fds[1], "x", 1) == 1);
    {
      std::unique_lock<std::mutex> lock(m);
      CHECK(cv.wait_for(lock, std::chrono::seconds(10), [&] { return fired_fd != -1; }));
    }
    CHECK(fired_fd == fds[0]);
    CHECK(received == 'x');

    service.untrack(socket);
    service.wait_for_removal(socket);
    close(fds[0]);
    close(fds[1]);
  }
#endif

#ifdef _WIN32
  WSACleanup();
#endif
  return failures == 0 ? 0 : 1;
}
