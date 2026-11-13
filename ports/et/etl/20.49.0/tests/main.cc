// Written in C++03, the standard upstream supports, with no etl_profile.h.
#include <etl/algorithm.h>
#include <etl/bitset.h>
#include <etl/circular_buffer.h>
#include <etl/crc16.h>
#include <etl/crc16_ccitt.h>
#include <etl/crc32.h>
#include <etl/delegate.h>
#include <etl/format_spec.h>
#include <etl/limits.h>
#include <etl/list.h>
#include <etl/map.h>
#include <etl/string.h>
#include <etl/to_string.h>
#include <etl/type_traits.h>
#include <etl/u16string.h>
#include <etl/vector.h>
#include <etl/version.h>

#include <stdio.h>
#include <string.h>

// C++03 template arguments need external linkage, so the delegate targets
// stay outside the unnamed namespace.
int Twice(int x) { return 2 * x; }

struct Accumulator {
  int total;
  int Add(int x) {
    total += x;
    return total;
  }
};

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

struct Entry {
  int key;
  int order;
};

bool ByKey(const Entry& a, const Entry& b) { return a.key < b.key; }

void TestContainers() {
  etl::vector<int, 6> v;
  const int values[] = {42, 7, 19, 3, 25};
  v.assign(values, values + 5);
  etl::sort(v.begin(), v.end());
  const int sorted[] = {3, 7, 19, 25, 42};
  expect(v.size() == 5 && v.max_size() == 6 && v.available() == 1 &&
             etl::equal(v.begin(), v.end(), sorted),
         "vector sort");
  v.push_back(1);
  expect(v.full(), "vector full at capacity");

  // A full circular buffer drops its oldest element.
  etl::circular_buffer<int, 3> ring;
  for (int i = 1; i <= 5; ++i) ring.push(i);
  expect(ring.full() && ring.size() == 3 && ring.front() == 3 &&
             ring.back() == 5,
         "circular buffer overwrite");

  etl::map<int, char, 4> map;
  map.insert(etl::make_pair(30, 'c'));
  map.insert(etl::make_pair(10, 'a'));
  map.insert(etl::make_pair(20, 'b'));
  expect(map.size() == 3 && map.begin()->first == 10 && map.find(20) != map.end() &&
             map.find(20)->second == 'b' && map.find(15) == map.end(),
         "map ordering and lookup");
  map.erase(10);
  expect(map.size() == 2 && map.begin()->first == 20, "map erase");

  // list::sort is a stable merge sort: equal keys keep insertion order.
  etl::list<Entry, 8> list;
  const int keys[] = {3, 1, 2, 1, 3, 2};
  for (int i = 0; i < 6; ++i) {
    Entry e = {keys[i], i};
    list.push_back(e);
  }
  list.sort(ByKey);
  const int keys_after[] = {1, 1, 2, 2, 3, 3};
  const int order_after[] = {1, 3, 2, 5, 0, 4};
  bool stable = list.size() == 6;
  int i = 0;
  for (etl::list<Entry, 8>::const_iterator it = list.begin();
       stable && it != list.end(); ++it, ++i)
    stable = it->key == keys_after[i] && it->order == order_after[i];
  expect(stable, "list stable sort");
}

void TestStrings() {
  etl::string<10> text("hello");
  text.append(" world!");
  expect(text == "hello worl" && text.size() == 10 && text.is_truncated(),
         "string truncates at capacity");

  etl::string<16> number;
  etl::to_string(-1234, number);
  expect(number == "-1234", "to_string decimal");
  etl::format_spec spec;
  spec.hex().width(8).fill('0').upper_case(true);
  etl::to_string(0xBEEFu, number, spec);
  expect(number == "0000BEEF", "to_string hex");
}

void TestChecksums() {
  // The catalogue check values for the ASCII bytes "123456789".
  const char* digits = "123456789";
  const char* end = digits + strlen(digits);
  expect(etl::crc32(digits, end).value() == 0xCBF43926UL, "crc32");
  expect(etl::crc16(digits, end).value() == 0xBB3D, "crc16 (ARC)");
  etl::crc16_ccitt ccitt;
  ccitt.add(digits, digits + 4);
  ccitt.add(digits + 4, end);
  expect(ccitt.value() == 0x29B1, "crc16 ccitt, incremental");
}

void TestBitsAndDelegates() {
  etl::bitset<16> bits;
  bits.set(0).set(4).set(15);
  expect(bits.count() == 3 && bits.test(4) && !bits.test(5) &&
             bits.to_ulong() == 0x8011UL,
         "bitset");
  bits.flip();
  expect(bits.count() == 13 && bits.to_ulong() == 0x7FEEUL, "bitset flip");

  etl::delegate<int(int)> twice = etl::delegate<int(int)>::create<Twice>();
  expect(twice.is_valid() && twice(21) == 42, "delegate to function");
  Accumulator acc = {0};
  etl::delegate<int(int)> add =
      etl::delegate<int(int)>::create<Accumulator, &Accumulator::Add>(acc);
  add(5);
  expect(add(7) == 12 && acc.total == 12, "delegate to member function");
  etl::delegate<int(int)> empty;
  expect(!empty.is_valid(), "empty delegate");
}

// Before C++11, char16_t is libc++'s own type (patch 0001) or ETL's typedef.
void TestChar16() {
  const char16_t text[] = {'e', 't', 'l', 0};
  etl::u16string<4> s(text);
  s.push_back(char16_t('!'));
  expect(s.size() == 4 && s.full() && s[0] == char16_t('e') &&
             s[3] == char16_t('!'),
         "u16string");
  expect(etl::is_integral<char16_t>::value &&
             etl::numeric_limits<char16_t>::digits == 16 &&
             etl::numeric_limits<char16_t>::max() == 0xFFFF,
         "char16_t traits");
}

}  // namespace

int main() {
  expect(ETL_VERSION_MAJOR == 20 && ETL_VERSION_MINOR == 49 &&
             ETL_VERSION_PATCH == 0,
         "ETL version 20.49.0");
  TestContainers();
  TestStrings();
  TestChecksums();
  TestBitsAndDelegates();
  TestChar16();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("etl consumer test passed");
  return 0;
}
