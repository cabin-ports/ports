// Exercises slick::stream_buffer in local and shared memory: prepare, commit
// and consume publishing committed bytes as discrete records, the clamps on
// commit and consume, discard, reset, read and read_last, late joiners, the
// relocation of unconsumed bytes when a prepare would cross the end of the
// ring and the data lap it causes, a lagging reader skipping records the
// control ring overwrote (counted only with count_loss traits), argument
// errors, a second mapping attached by name and a geometry mismatch, and one
// producer thread broadcasting to two consumer threads.
#include <slick/stream_buffer.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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

bool is(std::pair<const std::uint8_t*, std::uint32_t> got, const char* text) {
  const std::size_t n = std::strlen(text);
  return got.first != nullptr && got.second == n && std::memcmp(got.first, text, n) == 0;
}

void publish(slick::stream_buffer& buf, const char* text) {
  const std::size_t n = std::strlen(text);
  auto region = buf.prepare(n);
  std::memcpy(region.first, text, n);
  buf.commit(n);
  buf.consume(n);
}

struct counting : slick::read_traits {
  static constexpr bool count_loss = true;
};
struct not_bool {
  static constexpr int detect_reset = 1;
  static constexpr bool count_loss = false;
};
static_assert(slick::read_traits_type<counting> && !slick::read_traits_type<not_bool>);

void local_buffer() {
  slick::stream_buffer buf(64, 8);
  CHECK(buf.capacity() == 64 && buf.control_size() == 8 && !buf.use_shm() && buf.own_buffer() &&
        std::strcmp(buf.shm_name(), "") == 0 && !buf.remove_shm());
  std::uint64_t cursor = buf.initial_reading_index();
  CHECK(cursor == 0 && buf.read(cursor).first == nullptr && buf.read_last().first == nullptr);

  // Bytes committed as 3 + 8 are published as records of 5 and 6: consume,
  // not commit, sets the record boundaries.
  auto region = buf.prepare(11);
  CHECK(region.second == 11);
  std::memcpy(region.first, "hello world", 11);
  buf.commit(3);
  CHECK(buf.size() == 3 && std::memcmp(buf.data(), "hel", 3) == 0);
  buf.commit(100);  // clamped to the eight bytes still prepared
  CHECK(buf.size() == 11);
  const auto first = buf.consume(5);
  CHECK(first && first.sequence == 0 && first.length == 5 &&
        std::memcmp(first.data, "hello", 5) == 0);
  CHECK(buf.size() == 6 && std::memcmp(buf.data(), " world", 6) == 0);
  const auto second = buf.consume(100);  // clamped to size()
  CHECK(second && second.sequence == 1 && second.length == 6 && buf.size() == 0);
  CHECK(!buf.consume(10));

  CHECK(is(buf.read(cursor), "hello") && cursor == 1);
  CHECK(is(buf.read(cursor), " world") && cursor == 2);
  CHECK(buf.read(cursor).first == nullptr);
  CHECK(buf.read_last().first == second.data && is(buf.read_last(), " world"));
  CHECK(buf.initial_reading_index() == 2);

  // Discarded bytes are never published, and a stale commit commits nothing.
  region = buf.prepare(4);
  std::memcpy(region.first, "junk", 4);
  buf.commit(4);
  buf.discard();
  buf.commit(4);
  CHECK(buf.size() == 0);
  publish(buf, "next");
  CHECK(is(buf.read(cursor), "next") && cursor == 3);

  buf.reset();
  CHECK(buf.initial_reading_index() == 0 && buf.size() == 0 && buf.read_last().first == nullptr);
  publish(buf, "again");
  std::uint64_t fresh = 0;
  CHECK(is(buf.read(fresh), "again") && fresh == 1);

  CHECK(throws<std::length_error>([&] { buf.prepare(65); }));
  CHECK(throws<std::invalid_argument>([] { slick::stream_buffer bad(48, 8); }));
  CHECK(throws<std::invalid_argument>([] { slick::stream_buffer bad(64, 6); }));
}

void wrap() {
  // Four bytes stay unconsumed at ring offset 6.  Eight more do not fit
  // before the end of the 16-byte ring, so prepare moves the four to the
  // start and the first record's bytes, at 0-5, are overwritten.
  slick::stream_buffer buf(16, 8);
  auto region = buf.prepare(10);
  std::memcpy(region.first, "0123456789", 10);
  buf.commit(10);
  CHECK(buf.consume(6).length == 6);
  region = buf.prepare(8);
  CHECK(std::memcmp(buf.data(), "6789", 4) == 0 && buf.size() == 4 &&
        region.first == buf.data() + 4);
  std::memcpy(region.first, "ABCDEFGH", 8);
  buf.commit(8);
  const auto record = buf.consume(12);
  CHECK(record.sequence == 1 && record.length == 12);

  // Both readers skip the overwritten record; only counting traits count it.
  std::uint64_t plain = 0;
  CHECK(is(buf.read(plain), "6789ABCDEFGH") && plain == 2 && buf.loss_count() == 0);
  std::uint64_t counted = 0;
  CHECK(is(buf.read<counting>(counted), "6789ABCDEFGH") && counted == 2 &&
        buf.loss_count() == 1);
}

void lapped_reader() {
  // Six records into a four-record control ring: slots 0 and 1 now hold
  // records 4 and 5, so a reader still at 0 jumps to 4, skipping four.
  slick::stream_buffer buf(1024, 4);
  const char* const texts[] = {"a", "b", "c", "d", "e", "f"};
  for (const char* text : texts) publish(buf, text);
  std::uint64_t cursor = 0;
  CHECK(is(buf.read<counting>(cursor), "e") && cursor == 5 && buf.loss_count() == 4);
  CHECK(is(buf.read<counting>(cursor), "f") && cursor == 6);
  CHECK(buf.read<counting>(cursor).first == nullptr);
  CHECK(is(buf.read_last(), "f"));
}

void shared_buffer() {
  // Short enough for macOS, whose POSIX shared memory names stop at 31 bytes.
  std::random_device device;
  const unsigned long long tag =
      (static_cast<unsigned long long>(device()) << 16) ^
      static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
  char name[32];
  char missing[32];
  std::snprintf(name, sizeof name, "ssb-%012llx", tag & 0xffffffffffffULL);
  std::snprintf(missing, sizeof missing, "ssbm-%012llx", tag & 0xffffffffffffULL);

  CHECK(throws<std::runtime_error>([&] { slick::stream_buffer absent(missing); }));
  {
    slick::stream_buffer creator(1024, 16, name);
    CHECK(creator.use_shm() && creator.own_buffer() && std::strcmp(creator.shm_name(), name) == 0);
    publish(creator, "early");

    slick::stream_buffer peer(name);
    CHECK(peer.use_shm() && !peer.own_buffer() && peer.capacity() == 1024 &&
          peer.control_size() == 16 && peer.initial_reading_index() == 1);
    slick::stream_buffer second_creator(1024, 16, name);
    CHECK(!second_creator.own_buffer());

    bool mismatch = false;
    try {
      slick::stream_buffer wrong(2048, 16, name);
    } catch (const std::runtime_error& e) {
      mismatch = std::string(e.what()).find("geometry mismatch") != std::string::npos;
    }
    CHECK(mismatch);

    std::uint64_t cursor = 0;
    CHECK(is(peer.read(cursor), "early"));
    publish(creator, "late");
    CHECK(is(peer.read(cursor), "late") && cursor == 2 && is(second_creator.read_last(), "late"));

    CHECK(creator.remove_shm());
  }
  CHECK(!slick::shm::shared_memory::exists(name));
}

void threads() {
  // The rings hold every record, so each consumer must see all of them, in
  // order: a four-byte index followed by index % 7 copies of its low byte.
  constexpr std::uint32_t count = 3000;
  slick::stream_buffer buf(1 << 16, 4096);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

  std::atomic<std::uint32_t> complete{0};
  std::vector<std::thread> workers;
  for (int c = 0; c < 2; ++c) {
    workers.emplace_back([&] {
      std::uint64_t cursor = 0;
      std::uint32_t expected = 0;
      while (expected < count && std::chrono::steady_clock::now() < deadline) {
        const auto got = buf.read(cursor);
        if (got.first == nullptr) {
          std::this_thread::yield();
          continue;
        }
        std::uint32_t index = 0;
        bool ok = got.second == 4 + expected % 7;
        if (ok) std::memcpy(&index, got.first, 4);
        ok = ok && index == expected;
        for (std::uint32_t k = 4; ok && k < got.second; ++k)
          ok = got.first[k] == static_cast<std::uint8_t>(expected);
        if (!ok) break;
        ++expected;
      }
      if (expected == count) complete.fetch_add(1);
    });
  }
  workers.emplace_back([&] {
    for (std::uint32_t i = 0; i < count; ++i) {
      const std::size_t n = 4 + i % 7;
      auto region = buf.prepare(n);
      std::memcpy(region.first, &i, 4);
      std::memset(region.first + 4, static_cast<std::uint8_t>(i), n - 4);
      buf.commit(n);
      buf.consume(n);
    }
  });
  for (auto& w : workers) w.join();
  CHECK(complete.load() == 2);
}

}  // namespace

int main() {
  local_buffer();
  wrap();
  lapped_reader();
  shared_buffer();
  threads();
  std::printf("slick-stream-buffer: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
