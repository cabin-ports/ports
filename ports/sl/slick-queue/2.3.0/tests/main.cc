// Exercises slick::queue in local and shared memory: single and multi-item
// reservations, the jump to the next lap when a reservation would not fit,
// read_last, what a lagging reader sees once producers overrun it (counted
// by debug_queue_traits), items_per_slot, argument errors, a second mapping
// attached by name, and producers and consumers on several threads sharing
// an atomic cursor.
#include <slick/queue.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

template <typename E, typename F>
bool throws(F f) {
  try {
    f();
  } catch (const E&) {
    return true;
  } catch (...) {
  }
  return false;
}

void local_queue() {
  slick::queue<int> q(8);
  CHECK(q.size() == 8 && q.items_per_slot() == 1 && !q.use_shm());
  std::uint64_t cursor = q.initial_reading_index();
  CHECK(cursor == 0);
  CHECK(q.read(cursor).first == nullptr);
  CHECK(q.read_last().first == nullptr);

  const auto one = q.reserve();
  *q[one] = 42;
  q.publish(one);
  auto got = q.read(cursor);
  CHECK(got.first != nullptr && *got.first == 42 && got.second == 1 && cursor == 1);
  CHECK(q.read(cursor).first == nullptr);

  const auto three = q.reserve(3);
  CHECK(three == 1);
  for (int k = 0; k < 3; ++k) q[three][k] = 10 + k;
  q.publish(three, 3);
  got = q.read(cursor);
  CHECK(got.first != nullptr && got.second == 3 && got.first[0] == 10 && got.first[2] == 12 &&
        cursor == 4);
  CHECK(q.read_last().first == q[three] && q.read_last().second == 3);

  // Indices 4-6 fit; three more from 7 would cross the end of the ring, so
  // the reservation starts the next lap at 8 and the reader skips slot 7.
  const auto fits = q.reserve(3);
  q.publish(fits, 3);
  CHECK(fits == 4 && q.read(cursor).second == 3 && cursor == 7);
  const auto wrapped = q.reserve(3);
  CHECK(wrapped == 8);
  for (int k = 0; k < 3; ++k) q[wrapped][k] = 20 + k;
  q.publish(wrapped, 3);
  got = q.read(cursor);
  CHECK(got.first == q[wrapped] && got.second == 3 && got.first[1] == 21 && cursor == 11);

  CHECK(throws<std::invalid_argument>([&] { q.reserve(0); }));
  CHECK(throws<std::runtime_error>([&] { q.reserve(9); }));
  CHECK(throws<std::invalid_argument>([] { slick::queue<int> bad(6); }));
  CHECK(!q.remove_shm());
}

void overrun() {
  // The queue is lossy: six items into four slots overwrite the first two,
  // and a reader still at 0 lands on item 4, skipping four.
  slick::queue<int, slick::debug_queue_traits> q(4);
  for (int k = 0; k < 6; ++k) {
    const auto i = q.reserve();
    *q[i] = k;
    q.publish(i);
  }
  std::uint64_t cursor = 0;
  auto got = q.read(cursor);
  CHECK(got.first != nullptr && *got.first == 4 && cursor == 5 && q.loss_count() == 4);
  got = q.read(cursor);
  CHECK(got.first != nullptr && *got.first == 5 && cursor == 6);
  CHECK(q.read(cursor).first == nullptr);
  CHECK(*q.read_last().first == 5);
}

void items_per_slot() {
  // Each control slot covers eight bytes, so a reservation consumes whole
  // multiples of eight while read() reports the published size.
  slick::queue<char> bytes(64, 8);
  CHECK(bytes.items_per_slot() == 8 && bytes.slot_count() == 8);
  const auto small = bytes.reserve(3);
  std::memcpy(bytes[small], "abc", 3);
  bytes.publish(small, 3);
  const auto large = bytes.reserve(10);
  CHECK(small == 0 && large == 8);
  std::memcpy(bytes[large], "0123456789", 10);
  bytes.publish(large, 10);

  std::uint64_t cursor = 0;
  auto got = bytes.read(cursor);
  CHECK(got.second == 3 && std::memcmp(got.first, "abc", 3) == 0 && cursor == 8);
  got = bytes.read(cursor);
  CHECK(got.second == 10 && std::memcmp(got.first, "0123456789", 10) == 0 && cursor == 24);

  CHECK(throws<std::invalid_argument>([] { slick::queue<char> bad(64, 3); }));
  CHECK(throws<std::invalid_argument>([] { slick::queue<char> bad(8, 16); }));
}

void shared_queue() {
  // Short enough for macOS, whose POSIX shared memory names stop at 31 bytes.
  std::random_device device;
  const unsigned long long tag =
      (static_cast<unsigned long long>(device()) << 16) ^
      static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
  char name[32];
  char missing[32];
  std::snprintf(name, sizeof name, "slq-%012llx", tag & 0xffffffffffffULL);
  std::snprintf(missing, sizeof missing, "slqm-%012llx", tag & 0xffffffffffffULL);

  CHECK(throws<std::runtime_error>([&] { slick::queue<int> absent(missing); }));
  {
    slick::queue<int> creator(16, name);
    slick::queue<int> peer(name);
    CHECK(creator.use_shm() && std::strcmp(creator.shm_name(), name) == 0);
    CHECK(peer.use_shm() && peer.size() == 16 && peer.initial_reading_index() == 0);

    const auto i = creator.reserve(2);
    creator[i][0] = 7;
    creator[i][1] = 8;
    creator.publish(i, 2);
    std::uint64_t cursor = 0;
    auto got = peer.read(cursor);
    CHECK(got.first != nullptr && got.second == 2 && got.first[0] == 7 && got.first[1] == 8);

    const auto j = peer.reserve();
    *peer[j] = 9;
    peer.publish(j);
    got = creator.read(cursor);
    CHECK(j == 2 && got.first != nullptr && *got.first == 9 && cursor == 3);
    CHECK(*creator.read_last().first == 9);

    CHECK(creator.remove_shm());
  }
  CHECK(!slick::shm::shared_memory::exists(name));
}

void threads() {
  // The ring holds every item, so nothing is overwritten and each must be
  // read exactly once.
  constexpr int producers = 3;
  constexpr int per_producer = 2000;
  constexpr int total = producers * per_producer;
  slick::queue<int> q(8192);
  std::atomic<std::uint64_t> cursor{0};
  std::atomic<int> consumed{0};
  std::vector<std::atomic<int>> seen(total);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

  std::vector<std::thread> workers;
  for (int p = 0; p < producers; ++p) {
    workers.emplace_back([&, p] {
      for (int k = 0; k < per_producer; ++k) {
        const auto i = q.reserve();
        *q[i] = p * per_producer + k;
        q.publish(i);
      }
    });
  }
  for (int c = 0; c < 2; ++c) {
    workers.emplace_back([&] {
      while (consumed.load() < total && std::chrono::steady_clock::now() < deadline) {
        const auto got = q.read(cursor);
        if (got.first == nullptr) {
          std::this_thread::yield();
          continue;
        }
        if (*got.first >= 0 && *got.first < total) seen[*got.first].fetch_add(1);
        consumed.fetch_add(1);
      }
    });
  }
  for (auto& w : workers) w.join();

  bool once = consumed.load() == total;
  for (const auto& s : seen) once = once && s.load() == 1;
  CHECK(once);
}

}  // namespace

int main() {
  local_queue();
  overrun();
  items_per_slot();
  shared_queue();
  threads();
  std::printf("slick-queue: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
