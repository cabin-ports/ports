// The C++20 constexpr and concepts parts of plf_tools.h, which the C++98
// consumer test cannot reach.  Deliberately includes no <concepts>.
#define PLF_INCLUDE_TOOLS
#define PLF_INCLUDE_BIT_TOOLS
#define PLF_INCLUDE_UNINITIALIZED_TOOLS
#include <plf_tools.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#ifndef PLF_CPP20_SUPPORT
// MSVC without /Zc:__cplusplus reports C++98, so the header stays out of its
// C++20 mode.
int main() { return 0; }
#else
#if !defined(PLF_CONSTEVAL) || !defined(PLF_DECLTYPE_SUPPORT) || \
    !defined(PLF_64BIT_SUPPORT)
#error "plf_tools.h does not detect the C++20 features of this toolchain"
#endif

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

constexpr bool under_constant_evaluation() {
  if PLF_CONSTEVAL {
    return true;
  }
  return false;
}
static_assert(under_constant_evaluation());

constexpr int three_value = 3;  // equal_to keeps a reference to it
constexpr plf::equal_to<int> three(three_value);
static_assert(plf::less<int>()(1, 2) && !plf::less<int>()(2, 1));
static_assert(three(3) && !three(4));

// plf::countr_zero and friends take their constant-evaluation path to
// std::countr_zero and friends.
static_assert(plf::popcount(0xF0F0u) == 8);
static_assert(plf::countr_zero(8u) == 3 && plf::countl_zero(1u) == 31);
static_assert(plf::countr_one(7u) == 3 && plf::countl_one(0xF0000000u) == 4);
static_assert(plf::countl_zero(static_cast<unsigned char>(1)) == 7);
static_assert(plf::countl_one(static_cast<unsigned char>(0xF0)) == 4);

constexpr std::array<char, 8> low_first = [] {
  std::array<char, 8> out{};
  const unsigned char bytes[] = {0xA5};
  plf::bits_to_chars<false>(bytes, out.data(), '0', '1', out.size());
  return out;
}();
static_assert(low_first[0] == '1' && low_first[1] == '0' &&
              low_first[2] == '1' && low_first[5] == '1' &&
              low_first[7] == '1' && low_first[6] == '0');

// A std::allocator has no construct member here, so the uninitialized tools
// take the allocator_traits path, which constant evaluation can run.
constexpr int copied_and_filled = [] {
  std::allocator<int> alloc;
  const int source[] = {1, 2, 3};
  int *copies = alloc.allocate(3);
  plf::uninitialized_copy(source, source + 3, copies, alloc);
  int *fills = alloc.allocate(2);
  plf::uninitialized_fill_n(fills, 2, 7, alloc);
  const int result = copies[0] * 10000 + copies[1] * 1000 + copies[2] * 100 +
                     fills[0] * 10 + fills[1];
  std::destroy_n(copies, 3);
  std::destroy_n(fills, 2);
  alloc.deallocate(copies, 3);
  alloc.deallocate(fills, 2);
  return result;
}();
static_assert(copied_and_filled == 12377);

static_assert(plf::compatible_range<std::vector<int>, long>);
static_assert(!plf::compatible_range<std::vector<std::string>, int>);
static_assert(!plf::compatible_range<int, int>);

// A fancy pointer: construct() must receive the raw address, not this.
template <class T>
struct box_ptr {
  // The std::uninitialized_* branch is compiled too, so this is a full iterator.
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using reference = T &;
  T *raw;
  T &operator*() const { return *raw; }
  T *operator->() const { return raw; }
  box_ptr &operator++() {
    ++raw;
    return *this;
  }
  bool operator!=(const box_ptr &other) const { return raw != other.raw; }
};

int constructs = 0;

template <class T>
struct counting_allocator : std::allocator<T> {
  template <class U>
  void construct(U *p, const U &value) {
    ++constructs;
    ::new (static_cast<void *>(p)) U(value);
  }
};

}  // namespace

int main() {
  check(!under_constant_evaluation(), "PLF_CONSTEVAL is false at run time");

  counting_allocator<std::string> alloc;
  std::string *storage = alloc.allocate(3);
  plf::uninitialized_fill_n(box_ptr<std::string>{storage}, 3,
                            std::string("fancy"), alloc);
  check(constructs == 3 && storage[0] == "fancy" && storage[2] == "fancy",
        "uninitialized_fill_n with a fancy pointer");
  const std::string names[] = {"a", "b"};
  plf::uninitialized_copy(names, names + 2, box_ptr<std::string>{storage + 1},
                          alloc);
  check(constructs == 5 && storage[1] == "a" && storage[2] == "b",
        "uninitialized_copy to a fancy pointer");
  std::destroy_n(storage, 3);
  alloc.deallocate(storage, 3);

  // The 64-bit and MSB-first paths need C++11, which the C++98 test skips.
  check(plf::countr_zero((1ULL << 40) | (1ULL << 20)) == 20 &&
            plf::countl_zero(1ULL << 20) == 43 && plf::popcount(~0ULL) == 64,
        "64-bit bit tools");
  const unsigned char bytes[] = {0xA5, 0x00, 0x01};
  std::string high_first(20, '0');
  plf::bits_to_chars<true>(bytes, &high_first[0], '0', '1', high_first.size());
  check(high_first == "00010000000010100101", "bits_to_chars, MSB first");
  const unsigned long long words[] = {0xF0, 0x5};
  std::string spread(70, '0');
  plf::bits_to_chars<false>(words, &spread[0], '0', '1', spread.size());
  check(spread == "00001111" + std::string(56, '0') + "101000",
        "bits_to_chars across 64-bit words");

  if (failures != 0) {
    return 1;
  }
  std::printf("plf-tools c++20: %d\n", copied_and_filled);
  return 0;
}

#endif  // PLF_CPP20_SUPPORT
