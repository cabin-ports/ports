#include <ringbuffer.hpp>

#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);    \
      ++failures;                                                 \
    }                                                             \
  } while (0)

int main() {
  jnk0le::Ringbuffer<int, 4> rb;
  CHECK(rb.isEmpty() && !rb.isFull());
  CHECK(rb.readAvailable() == 0 && rb.writeAvailable() == 4);
  CHECK(rb.peek() == nullptr);

  for (int i = 1; i <= 4; ++i) CHECK(rb.insert(i));
  CHECK(rb.isFull() && !rb.isEmpty());
  CHECK(!rb.insert(99));
  CHECK(rb.readAvailable() == 4 && rb.writeAvailable() == 0);
  CHECK(rb.peek() != nullptr && *rb.peek() == 1);
  CHECK(*rb.at(2) == 3 && rb.at(4) == nullptr);

  int v = 0;
  CHECK(rb.remove(v) && v == 1);
  // The head index has wrapped past the end of storage here.
  CHECK(rb.insert(5));
  CHECK(!rb.insert(6));
  CHECK(rb[3] == 5);
  CHECK(rb.remove(1) == 1);  // drop 2
  for (int expect = 3; expect <= 5; ++expect) {
    CHECK(rb.remove(v) && v == expect);
  }
  CHECK(!rb.remove(v) && rb.isEmpty());

  rb.insert(9);
  CHECK(*rb.peek() == 9);
  rb.consumerClear();
  CHECK(rb.isEmpty());

  // Bulk transfer across the wrap point of the 8-slot buffer.
  jnk0le::Ringbuffer<char, 8> cb;
  CHECK(cb.writeBuff("abcdef", 6) == 6);
  char out[8] = {};
  CHECK(cb.readBuff(out, 4) == 4);
  CHECK(out[0] == 'a' && out[3] == 'd');
  CHECK(cb.writeBuff("ghijk", 5) == 5);  // wraps; 7 of 8 slots used
  CHECK(cb.writeBuff("lm", 2) == 1);     // only 'l' fits
  CHECK(cb.writeAvailable() == 0);
  char all[9] = {};
  CHECK(cb.readBuff(all, 9) == 8);
  CHECK(std::strcmp(all, "efghijkl") == 0);
  CHECK(cb.isEmpty());
  return failures == 0 ? 0 : 1;
}
