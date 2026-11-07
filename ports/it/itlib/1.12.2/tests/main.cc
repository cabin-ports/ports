// itlib at its C++11 floor: small_vector's static and dynamic buffers,
// static_vector's capacity error, flat_map (also over a static_vector) and
// flat_set ordering and lookup, expected, span and stride_span views,
// dynamic_bitset, pod_vector recasts, the qalgorithm wrappers, ufunction with
// a move-only callable, sentry, any with move-only and over-aligned values,
// ref_ptr, time_t's UTC conversions, the stream buffers, throw_ex, bit_cast,
// make_ptr, data_mutex, and atomic_shared_ptr_storage.  The other C++11
// headers are included so that each compiles at the floor.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <istream>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include <itlib/any.hpp>
#include <itlib/atomic.hpp>
#include <itlib/atomic_shared_ptr_storage.hpp>
#include <itlib/data_mutex.hpp>
#include <itlib/dynamic_bitset.hpp>
#include <itlib/expected.hpp>
#include <itlib/flat_map.hpp>
#include <itlib/flat_set.hpp>
#include <itlib/make_ptr.hpp>
#include <itlib/mem_streambuf.hpp>
#include <itlib/memory_view.hpp>
#include <itlib/mutex.hpp>
#include <itlib/pod_vector.hpp>
#include <itlib/poly_span.hpp>
#include <itlib/qalgorithm.hpp>
#include <itlib/ref_ptr.hpp>
#include <itlib/rstream.hpp>
#include <itlib/sentry.hpp>
#include <itlib/shared_from.hpp>
#include <itlib/small_vector.hpp>
#include <itlib/span.hpp>
#include <itlib/static_vector.hpp>
#include <itlib/stride_span.hpp>
#include <itlib/tep_vector.hpp>
#include <itlib/throw_ex.hpp>
#include <itlib/time_t.hpp>
#include <itlib/type_traits.hpp>
#include <itlib/ufunction.hpp>
#include <itlib/utility.hpp>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

template <typename Range>
std::vector<int> ints(const Range& r) {
  return std::vector<int>(r.begin(), r.end());
}

void test_small_vector() {
  // The walk-through in small_vector.hpp:74-81: a static capacity of 4 and
  // a return to the static buffer once the size drops below 5.
  itlib::small_vector<int, 4, 5> v;
  v.resize(2);
  v[1] = 11;
  v.push_back(7);
  v.insert(v.begin() + 1, 3);
  CHECK(v.is_static());
  CHECK(ints(v) == (std::vector<int>{0, 3, 11, 7}));
  v.push_back(5);
  CHECK(!v.is_static());
  CHECK(ints(v) == (std::vector<int>{0, 3, 11, 7, 5}));
  v.erase(v.begin());
  CHECK(v.is_static());
  CHECK(ints(v) == (std::vector<int>{3, 11, 7, 5}));
  v.resize(5);
  CHECK(!v.is_static());
  CHECK(ints(v) == (std::vector<int>{3, 11, 7, 5, 0}));
}

void test_static_vector() {
  itlib::static_vector<int, 3> s;
  s.push_back(1);
  s.push_back(2);
  s.push_back(3);
  CHECK(s.size() == 3 && s.capacity() == 3);
  // Exceeding the capacity throws by default (static_vector.hpp:126-127).
  bool threw = false;
  try {
    s.push_back(4);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(ints(s) == (std::vector<int>{1, 2, 3}));
}

void test_flat_containers() {
  itlib::flat_map<std::string, int> m;
  m["pear"] = 3;
  m.emplace("apple", 1);
  m.insert(std::make_pair(std::string("fig"), 2));
  CHECK(m.size() == 3);
  CHECK(m.begin()->first == "apple" && (m.begin() + 1)->first == "fig" &&
        (m.begin() + 2)->first == "pear");
  CHECK(&*(m.begin() + 1) == &*m.begin() + 1);
  CHECK(m.at("fig") == 2);
  bool threw = false;
  try {
    m.at("kiwi");
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);
  m.erase("fig");
  CHECK(m.find("fig") == m.end() && m.size() == 2);

  // A static_vector as the underlying container (flat_map.hpp:69-70).
  itlib::flat_map<int, char, std::less<int>,
                  itlib::static_vector<std::pair<int, char>, 4>>
      sm;
  sm[30] = 'c';
  sm[10] = 'a';
  sm[20] = 'b';
  sm[10] = 'A';
  CHECK(sm.size() == 3 && sm.begin()->second == 'A' &&
        (sm.end() - 1)->first == 30);

  itlib::flat_set<int> fs;
  const int inserted[] = {5, 1, 5, 3};
  for (int x : inserted) fs.insert(x);
  CHECK(ints(fs) == (std::vector<int>{1, 3, 5}));
  CHECK(fs.find(3) != fs.end() && fs.find(4) == fs.end());
}

enum class div_error { by_zero };

itlib::expected<int, div_error> divide(int a, int b) {
  if (b == 0) return itlib::unexpected(div_error::by_zero);
  return a / b;
}

void test_expected() {
  auto q = divide(7, 2);
  CHECK(q && q.has_value() && *q == 3);
  auto e = divide(1, 0);
  CHECK(!e && e.has_error() && e.error() == div_error::by_zero);
  CHECK(e.value_or(-1) == -1);
  // Unlike std::optional, a default-constructed expected holds a value.
  itlib::expected<int, div_error> d;
  CHECK(d && *d == 0);
  itlib::expected<int, void> none = itlib::unexpected();
  CHECK(!none);
}

struct point {
  int x;
  int y;
};

void test_views() {
  int arr[] = {1, 2, 3, 4, 5};
  auto sp = itlib::make_span(arr);
  CHECK(sp.size() == 5 && sp[4] == 5);
  auto mid = sp.subspan(1, 3);
  CHECK(mid.size() == 3 && mid.front() == 2 && mid.back() == 4);
  mid[0] = 20;
  CHECK(arr[1] == 20);
  sp.remove_prefix(2);
  sp.remove_suffix(1);
  CHECK(sp.size() == 2 && sp[0] == 3 && sp[1] == 4);
  CHECK(sp.as_bytes().size() == 2 * sizeof(int));

  point pts[3] = {{1, 10}, {2, 20}, {3, 30}};
  auto ys = itlib::make_stride_span_from_buf<int>(pts, offsetof(point, y),
                                                  sizeof(point), 3);
  CHECK(ys.size() == 3 && ys[0] == 10 && ys[1] == 20 && ys[2] == 30);
  ys[1] = 21;
  CHECK(pts[1].y == 21 && pts[1].x == 2);
}

void test_dynamic_bitset() {
  itlib::dynamic_bitset<> bits(70);
  CHECK(bits.size() == 70 && bits.word_size() == 3 && bits.none());
  bits.set(3);
  bits.set(69);
  bits[40] = true;
  CHECK(bits.test(3) && bits[40] && bits.test(69) && !bits.test(4));
  CHECK(bits.data()[0] == (1u << 3) && bits.data()[1] == (1u << 8) &&
        bits.data()[2] == (1u << 5));
  bits.flip(3);
  CHECK(!bits.test(3) && bits.any());
  bits.push_back(true);
  CHECK(bits.size() == 71 && bits.test(70));
  CHECK(itlib::dynamic_bitset<>(5, 0x1Fu).all());
}

void test_pod_vector_and_algorithms() {
  itlib::pod_vector<uint32_t> words;
  words.push_back(0x01020304u);
  words.push_back(0xA0B0C0D0u);
  CHECK(words.byte_size() == 8);
  itlib::pod_vector<uint8_t> bytes;
  bytes.recast_copy_from(words);
  CHECK(bytes.size() == 8);
  uint32_t first = 0;
  std::memcpy(&first, bytes.data(), sizeof(first));
  CHECK(first == 0x01020304u);
  words.resize(1000);
  CHECK(words.size() == 1000 && words[0] == 0x01020304u &&
        words[1] == 0xA0B0C0D0u);

  std::vector<int> nums = {4, 1, 4, 2, 4};
  int* two = itlib::pfind(nums, 2);
  CHECK(two == &nums[3]);
  CHECK(itlib::pfind(nums, 9) == nullptr);
  CHECK(itlib::erase_all(nums, 4) == 3);
  CHECK(nums == (std::vector<int>{1, 2}));
  CHECK(itlib::erase_first(nums, 1) && nums == std::vector<int>{2});
  CHECK(itlib::qall_of(nums) && !itlib::qnone_of(nums));
}

// A move-only callable, which std::function cannot hold.
struct boxed_adder {
  std::unique_ptr<int> base;
  int operator()(int x) const { return *base + x; }
};

void test_callables() {
  typedef itlib::ufunction<int(int)> ufn;
  static_assert(std::is_nothrow_move_constructible<ufn>::value,
                "ufunction moves without throwing");
  static_assert(!std::is_copy_constructible<ufn>::value,
                "ufunction is not copyable");
  ufn f = boxed_adder{std::unique_ptr<int>(new int(40))};
  CHECK(f(2) == 42);
  ufn g = std::move(f);
  CHECK(g && g(1) == 41);

  int exits = 0;
  {
    auto s = itlib::make_sentry([&exits]() { ++exits; });
    CHECK(exits == 0);
  }
  CHECK(exits == 1);

  std::string msg;
  try {
    itlib::throw_ex<std::runtime_error>() << "bad value " << 42;
  } catch (const std::runtime_error& ex) {
    msg = ex.what();
  }
  CHECK(msg == "bad value 42");
}

struct alignas(32) wide {
  double v[4];
};

void test_any_and_pointers() {
  itlib::any<> a;
  CHECK(!a);
  a = std::string("hello");
  CHECK(a.has_value() && *a.tdata<std::string>() == "hello");
  CHECK(itlib::any_cast<std::string>(a) == "hello");
  itlib::any<> copy = a;
  CHECK(*copy.tdata<std::string>() == "hello" && copy.data() != a.data());
  a.emplace<std::unique_ptr<int>>(new int(9));
  CHECK(**a.tdata<std::unique_ptr<int>>() == 9);
  // Copying a non-copyable value throws std::bad_cast (any.hpp:46-47).
  bool threw = false;
  try {
    itlib::any<> again = a;
  } catch (const std::bad_cast&) {
    threw = true;
  }
  CHECK(threw);
  a.emplace<wide>();
  CHECK(reinterpret_cast<std::uintptr_t>(a.data()) % alignof(wide) == 0);

  auto r = itlib::make_ref_ptr<std::string>("cow");
  CHECK(r.unique());
  itlib::ref_ptr<const std::string> view = r;
  CHECK(!r.unique() && *view == "cow");
  view.reset();
  CHECK(r.unique());

  auto sp = itlib::make_shared(std::string("shared"));
  CHECK(*sp == "shared" && sp.use_count() == 1);
}

void test_time_and_streams() {
  std::tm leap = std::tm();
  leap.tm_year = 2024 - 1900;
  leap.tm_mon = 1;
  leap.tm_mday = 29;
  leap.tm_hour = 12;
  leap.tm_min = 34;
  leap.tm_sec = 56;
  auto t = itlib::time_t::from_gmtime(leap);
  CHECK(t.seconds_since_epoch() == 1709210096);
  CHECK(itlib::strftime("%Y-%m-%d %H:%M:%S", t.gmtime()) ==
        "2024-02-29 12:34:56");
  auto next = t + std::chrono::hours(24);
  CHECK(itlib::strftime("%Y-%m-%d", next.gmtime()) == "2024-03-01");
  CHECK(next - t == std::chrono::hours(24));

  // Before C++17 std::string::data() is const, so the container here is a
  // vector, as in upstream's C++11 test.
  itlib::mem_ostreambuf<std::vector<char>> obuf;
  std::ostream out(&obuf);
  out << "pi=" << 3 << '.' << 14;
  std::vector<char> written = obuf.get_container();
  CHECK(std::string(written.begin(), written.end()) == "pi=3.14");
  std::string text = "7 -8 9";
  itlib::mem_istreambuf<char> ibuf(text.data(), text.size());
  std::istream in(&ibuf);
  int a = 0, b = 0, c = 0;
  in >> a >> b >> c;
  CHECK(a == 7 && b == -8 && c == 9);

  CHECK(itlib::bit_cast<uint32_t>(1.0f) == 0x3f800000u);
  CHECK(itlib::bit_cast<float>(0xc0200000u) == -2.5f);
}

void test_synchronized() {
  itlib::data_mutex<std::vector<int>, std::mutex> guarded;
  guarded.unique_lock()->push_back(1);
  {
    auto l = guarded.unique_lock();
    l->push_back(2);
    CHECK(l->size() == 2);
  }
  {
    // try_lock may fail spuriously, so only a held lock is read.
    auto l = guarded.try_unique_lock();
    if (l) CHECK((*l)[1] == 2);
  }
  CHECK(*guarded.unique_lock() == (std::vector<int>{1, 2}));

  itlib::atomic_shared_ptr_storage<int> slot;
  CHECK(!slot.load());
  slot.store(std::make_shared<int>(15));
  CHECK(*slot.load() == 15);
  auto old = slot.exchange(std::make_shared<int>(16));
  CHECK(*old == 15 && *slot.load() == 16);
}

}  // namespace

int main() {
  test_small_vector();
  test_static_vector();
  test_flat_containers();
  test_expected();
  test_views();
  test_dynamic_bitset();
  test_pod_vector_and_algorithms();
  test_callables();
  test_any_and_pointers();
  test_time_and_streams();
  test_synchronized();
  if (failures != 0) return 1;
  std::puts("itlib 1.12.2: C++ API OK");
  return 0;
}
