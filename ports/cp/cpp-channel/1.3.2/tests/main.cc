#include <msd/channel.hpp>
#include <msd/static_channel.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

// A producer thread fills a buffered channel while this thread drains it
// with a range-for, which ends once the channel is closed and empty.
void test_buffered_producer_consumer() {
  msd::channel<int> ch{4};
  std::thread producer([&ch] {
    for (int i = 1; i <= 100; ++i) {
      ch << i;
    }
    ch.close();
  });

  int sum = 0;
  int previous = 0;
  bool ordered = true;
  for (int value : ch) {
    ordered = ordered && value == previous + 1;
    previous = value;
    sum += value;
  }
  producer.join();

  check(sum == 5050 && ordered, "values arrive once each, in order");
  check(ch.closed() && ch.drained() && ch.empty(), "closed and drained");
  check(!ch.write(1), "write on a closed channel returns false");
  bool threw = false;
  try {
    ch << 1;
  } catch (const msd::closed_channel&) {
    threw = true;
  }
  check(threw, "operator<< on a closed channel throws closed_channel");
  int out = 7;
  check(!ch.read(out) && out == 7, "read on a drained channel returns false");
}

// A static channel blocks its one writer while full; one writer and one
// reader are the only waiters, so each wakes the other.
void test_static_producer_consumer() {
  msd::static_channel<int, 2> ch;
  std::thread producer([&ch] {
    for (int i = 1; i <= 100; ++i) {
      ch << i;
    }
    ch.close();
  });

  int next = 1;
  bool ordered = true;
  for (int value : ch) {
    ordered = ordered && value == next;
    ++next;
  }
  producer.join();
  check(ordered && next == 101, "static channel blocks writes and keeps order");
}

// Several producers and consumers share an unbuffered channel, where only
// readers wait; a bounded one can deadlock with them (see cabin.toml).
void test_many_threads() {
  const int producers = 4;
  const int per_producer = 1000;
  msd::channel<int> ch;

  std::vector<std::thread> writers;
  for (int p = 0; p < producers; ++p) {
    writers.emplace_back([&ch, p] {
      for (int i = 0; i < per_producer; ++i) {
        ch << p * per_producer + i;
      }
    });
  }

  std::vector<long long> sums(2, 0);
  std::vector<int> counts(2, 0);
  std::vector<std::thread> readers;
  for (std::size_t r = 0; r < sums.size(); ++r) {
    readers.emplace_back([&ch, &sums, &counts, r] {
      int value = 0;
      while (ch.read(value)) {
        sums[r] += value;
        ++counts[r];
      }
    });
  }

  for (std::thread& t : writers) {
    t.join();
  }
  ch.close();
  for (std::thread& t : readers) {
    t.join();
  }

  const long long n = producers * per_producer;
  check(counts[0] + counts[1] == n, "every value is read once");
  check(sums[0] + sums[1] == n * (n - 1) / 2, "the values read sum as written");
}

void test_storages() {
  // array_storage wraps around its two slots.
  msd::static_channel<int, 2> fixed;
  int out = 0;
  fixed << 1 << 2;
  check(fixed.size() == 2, "static channel holds its capacity");
  fixed >> out;
  check(out == 1, "static channel is FIFO");
  fixed << 3;
  fixed >> out;
  check(out == 2, "static channel wraps around (first)");
  fixed >> out;
  check(out == 3 && fixed.empty(), "static channel wraps around (second)");

  msd::channel<std::string, msd::vector_storage<std::string>> strings{3};
  strings << std::string("a") << std::string("bc");
  std::string text;
  strings >> text;
  check(text == "a", "vector storage is FIFO (first)");
  strings >> text;
  check(text == "bc", "vector storage is FIFO (second)");

  // An unbuffered channel never blocks writes.
  msd::channel<int> unbounded;
  for (int i = 0; i < 1000; ++i) {
    unbounded << i;
  }
  check(unbounded.size() == 1000, "unbuffered channel takes every write");

  // Move-only values go in and out by move.
  msd::channel<std::unique_ptr<int>> owned{1};
  std::unique_ptr<int> in(new int(42));
  owned << std::move(in);
  std::unique_ptr<int> taken;
  owned >> taken;
  check(!in && taken && *taken == 42, "move-only values");
}

void test_iterators() {
  const std::vector<int> input{3, 1, 4, 1, 5, 9, 2, 6};
  msd::channel<int> ch{};
  std::copy(input.begin(), input.end(), msd::back_inserter(ch));
  ch.close();

  std::vector<int> output;
  std::copy(ch.begin(), ch.end(), std::back_inserter(output));
  check(output == input, "back_inserter and the blocking iterator round-trip");
}

}  // namespace

int main() {
  test_buffered_producer_consumer();
  test_static_producer_consumer();
  test_many_threads();
  test_storages();
  test_iterators();
  std::puts("cpp-channel 1.3.2 ok");
  return 0;
}
