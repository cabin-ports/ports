#include <CircularBuffer.hpp>

#include <cstdint>
#include <cstdio>
#include <type_traits>

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

using Buffer = CircularBuffer<int, 3>;

// The index type is the smallest that holds the capacity.
static_assert(std::is_same<Buffer::index_t, std::uint8_t>::value, "");
static_assert(
    std::is_same<CircularBuffer<char, 255>::index_t, std::uint8_t>::value, "");
static_assert(
    std::is_same<CircularBuffer<char, 256>::index_t, std::uint16_t>::value,
    "");
static_assert(
    std::is_same<CircularBuffer<char, 65535>::index_t, std::uint16_t>::value,
    "");
static_assert(
    std::is_same<CircularBuffer<char, 65536>::index_t, std::uint32_t>::value,
    "");

long twice(const int &value) { return 2L * value; }

} // namespace

int main() {
  // capacity has no out-of-line definition, so read it only by value.
  CHECK(Buffer::capacity == 3);

  Buffer b;
  CHECK(b.isEmpty());
  CHECK(!b.isFull());
  CHECK(b.available() == 3);
  CHECK(b.push(1));
  CHECK(!b.isEmpty());
  CHECK(b.push(2));
  CHECK(!b.isFull());
  CHECK(b.push(3));
  CHECK(b.isFull());
  CHECK(b.available() == 0);
  CHECK(!b.push(4)); // Full: overwrites the oldest, 1.

  CHECK(b.first() == 2);
  CHECK(b.last() == 4);
  CHECK(b[0] == 2);
  CHECK(b[1] == 3);
  CHECK(b[2] == 4);
  CHECK(b.size() == 3);

  CHECK(b.shift() == 2);
  CHECK(b.pop() == 4);
  CHECK(b.size() == 1);
  CHECK(b.available() == 2);

  CHECK(b.unshift(9));
  CHECK(b.first() == 9);
  CHECK(b.last() == 3);
  int out[2] = {0, 0};
  b.copyToArray(out);
  CHECK(out[0] == 9);
  CHECK(out[1] == 3);
  long wide[2] = {0, 0};
  b.copyToArray(wide, twice);
  CHECK(wide[0] == 18);
  CHECK(wide[1] == 6);

  b.clear();
  CHECK(b.isEmpty());
  CHECK(b.size() == 0);

  // unshift mirrors push, also on a full buffer.
  Buffer c;
  CHECK(c.unshift(1));
  CHECK(c.unshift(2));
  CHECK(c.unshift(3));
  CHECK(c.last() == 1);
  CHECK(!c.unshift(4)); // Full: drops the last, 1.
  CHECK(c.first() == 4);
  CHECK(c.last() == 2);
  CHECK(!c.unshift(5));
  CHECK(c.last() == 3);
  CHECK(!c.push(6)); // Full: drops the first, 5.
  CHECK(c.pop() == 6);
  CHECK(c.shift() == 4);
  // copyToArray writes exactly size() elements.
  int one[3] = {0, -1, -1};
  c.copyToArray(one);
  CHECK(one[0] == 3);
  CHECK(one[1] == -1);
  CHECK(one[2] == -1);
  CHECK(c.shift() == 3);
  CHECK(c.isEmpty());

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("circularbuffer ok");
  return 0;
}
