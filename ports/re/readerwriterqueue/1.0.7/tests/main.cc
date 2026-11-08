// Consumer test for cabin-ports/readerwriterqueue.  The two include
// spellings are upstream's installed-package and CMake-target forms.
#include <readerwriterqueue/readerwriterqueue.h>
#include <readerwritercircularbuffer.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

const int kItems = 100000;

void test_queue() {
  // Room for 15 elements in one block of 16 (one slot stays empty).
  moodycamel::ReaderWriterQueue<int> q(15);
  check(q.max_capacity() == 15, "reserved capacity");
  bool all = true;
  for (int i = 0; i < 15; ++i) all = q.try_enqueue(i) && all;
  check(all, "try_enqueue within capacity");
  check(!q.try_enqueue(15), "try_enqueue never allocates");
  check(q.enqueue(15) && q.size_approx() == 16 && q.max_capacity() > 15,
        "enqueue allocates another block");

  int *front = q.peek();
  check(front != nullptr && *front == 0, "peek");
  check(q.pop(), "pop");
  int v = -1;
  bool fifo = true;
  for (int i = 1; i <= 15; ++i) fifo = q.try_dequeue(v) && v == i && fifo;
  check(fifo, "FIFO order across blocks");
  check(!q.try_dequeue(v) && q.peek() == nullptr && !q.pop() && q.size_approx() == 0, "empty");

  // Empty again, both blocks take 15 + 31 elements without allocating,
  // reusing the first block once the second is full.
  check(q.max_capacity() == 46, "capacity after growth");
  int taken = 0;
  while (taken < 100 && q.try_enqueue(taken)) ++taken;
  check(taken == 46, "try_enqueue reuses free blocks");
  fifo = true;
  for (int i = 0; i < 46; ++i) fifo = q.try_dequeue(v) && v == i && fifo;
  check(fifo, "FIFO across reused blocks");

  // Above twice the block size the constructor reserves several blocks:
  // 10 elements in blocks of 4 take 5.
  moodycamel::ReaderWriterQueue<int, 4> multi(10);
  check(multi.max_capacity() == 15, "multi-block reservation");
  taken = 0;
  while (taken < 100 && multi.try_enqueue(taken)) ++taken;
  check(taken == 15, "reserved blocks fill without allocating");
  fifo = true;
  for (int i = 0; i < 15; ++i) fifo = multi.try_dequeue(v) && v == i && fifo;
  check(fifo && multi.size_approx() == 0, "FIFO across reserved blocks");

  moodycamel::ReaderWriterQueue<std::unique_ptr<int>> owned;
  check(owned.emplace(new int(7)) && owned.try_emplace(new int(8)), "emplace");
  std::unique_ptr<int> p;
  check(owned.try_dequeue(p) && p && *p == 7, "move-only dequeue");
  check(owned.try_dequeue(p) && p && *p == 8, "second move-only dequeue");
}

void test_blocking_queue() {
  moodycamel::BlockingReaderWriterQueue<int> q;
  int v = -1;
  check(!q.wait_dequeue_timed(v, 1000), "timed wait on an empty queue times out");
  check(!q.wait_dequeue_timed(v, std::chrono::milliseconds(1)), "chrono timed wait times out");
  check(v == -1, "a timeout leaves the target alone");

  std::thread producer([&q] {
    for (int i = 0; i < kItems; ++i) q.enqueue(i);
  });
  bool ordered = true;
  for (int i = 0; i < kItems; ++i) {
    q.wait_dequeue(v);
    if (v != i) ordered = false;
  }
  producer.join();
  check(ordered, "producer/consumer order");
  check(!q.try_dequeue(v) && q.size_approx() == 0, "drained");
}

void test_circular_buffer() {
  moodycamel::BlockingReaderWriterCircularBuffer<int> buf(3);
  check(buf.max_capacity() == 3, "circular capacity");
  check(buf.try_enqueue(1) && buf.try_enqueue(2) && buf.try_enqueue(3), "fill");
  check(!buf.try_enqueue(4), "try_enqueue on a full buffer fails");
  check(!buf.wait_enqueue_timed(4, 1000), "timed enqueue on a full buffer times out");
  check(buf.size_approx() == 3, "size_approx");

  int *front = buf.peek();
  check(front != nullptr && *front == 1, "circular peek");
  check(buf.try_pop(), "try_pop");
  check(buf.wait_enqueue_timed(4, std::chrono::milliseconds(1)), "timed enqueue with room");
  int v = 0;
  check(buf.try_dequeue(v) && v == 2 && buf.wait_dequeue_timed(v, 1000) && v == 3 &&
            buf.try_dequeue(v) && v == 4,
        "circular FIFO order");
  check(!buf.wait_dequeue_timed(v, 1000) && v == 4 && buf.peek() == nullptr, "circular empty");

  // A capacity far below the item count makes the producer block when full.
  std::thread producer([&buf] {
    for (int i = 0; i < kItems; ++i) buf.wait_enqueue(i);
  });
  bool ordered = true;
  for (int i = 0; i < kItems; ++i) {
    buf.wait_dequeue(v);
    if (v != i) ordered = false;
  }
  producer.join();
  check(ordered, "blocking circular buffer order");
}

}  // namespace

int main() {
  test_queue();
  test_blocking_queue();
  test_circular_buffer();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("readerwriterqueue consumer ok");
  return 0;
}
