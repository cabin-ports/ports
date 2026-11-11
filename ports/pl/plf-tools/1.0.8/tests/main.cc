// C++98-compatible: the probe builds this at the declared c++98.
#define PLF_INCLUDE_TOOLS
#define PLF_INCLUDE_BIT_TOOLS
#define PLF_INCLUDE_UNINITIALIZED_TOOLS
#include <plf_tools.h>

#if defined(PLF_INCLUDE_TOOLS) || defined(PLF_INCLUDE_BIT_TOOLS) || \
    defined(PLF_INCLUDE_UNINITIALIZED_TOOLS)
#error "plf_tools.h leaves the PLF_INCLUDE_ macros defined"
#endif
#if (__cplusplus >= 201103L || defined(_MSC_VER)) && !defined(PLF_CPP11_SUPPORT)
#error "plf_tools.h does not detect C++11 support"
#endif

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

// Compile-time checks without static_assert.
typedef char enable_if_true[sizeof(plf::enable_if<true, double>::type) ==
                                    sizeof(double)
                                ? 1
                                : -1];
typedef char conditional_false[sizeof(plf::conditional<false, char,
                                                       double>::type) ==
                                       sizeof(double)
                                   ? 1
                                   : -1];

int constructs = 0;

// An allocator with its own construct(), which the uninitialized tools must
// call instead of falling back to std::uninitialized_copy/fill_n.
template <class T>
struct counting_allocator : std::allocator<T> {
  template <class U>
  void construct(U *p, const U &value) {
    ++constructs;
    ::new (static_cast<void *>(p)) U(value);
  }
};

template <class T>
void destroy_all(T *p, std::size_t n) {
  for (std::size_t i = 0; i != n; ++i) {
    p[i].~T();
  }
}

void test_tools() {
  check(plf::less<int>()(1, 2) && !plf::less<int>()(2, 1), "plf::less");
  int values[] = {5, 3, 9, 3, 1};
  std::vector<int> sorted(values, values + 5);
  std::sort(sorted.begin(), sorted.end(), plf::less<int>());
  check(sorted[0] == 1 && sorted[2] == 3 && sorted[4] == 9,
        "std::sort with plf::less");
  int three = 3;
  check(std::count_if(values, values + 5, plf::equal_to<int>(three)) == 2,
        "plf::equal_to");

  check(plf::void_cast(&values[2]) == static_cast<void *>(&values[2]),
        "plf::void_cast");
  check(plf::pointer_cast<char *>(&values[1]) ==
            reinterpret_cast<char *>(&values[1]),
        "plf::pointer_cast");
  check(plf::performance == 1 && plf::memory_use == 4, "plf::priority");

#ifdef PLF_MOVE_SEMANTICS_SUPPORT
  std::vector<std::string> from(2, "moved");
  std::vector<std::string> to(plf::make_move_iterator(from.begin()),
                              plf::make_move_iterator(from.end()));
  check(to.size() == 2 && to[1] == "moved", "plf::make_move_iterator");
#endif
}

void test_bit_tools() {
  check(plf::popcount(0u) == 0 && plf::popcount(0xF0F0u) == 8 &&
            plf::popcount(0xFFFFFFFFu) == 32 &&
            plf::popcount(static_cast<unsigned char>(0x81)) == 2,
        "plf::popcount");
  check(plf::countr_zero(1u) == 0 && plf::countr_zero(0x80000000u) == 31 &&
            plf::countr_zero(0x100ul) == 8 &&
            plf::countr_zero(static_cast<unsigned char>(0x08)) == 3,
        "plf::countr_zero");
  check(plf::countl_zero(1u) == 31 && plf::countl_zero(0x80000000u) == 0 &&
            plf::countl_zero(0x00010000u) == 15 &&
            plf::countl_zero(1ul) == sizeof(unsigned long) * CHAR_BIT - 1,
        "plf::countl_zero");
  check(plf::countr_one(0x0000000Fu) == 4 &&
            plf::countl_one(0xF0000000u) == 4 &&
            plf::countl_one(0x7FFFFFFFu) == 0,
        "plf::countr_one and plf::countl_one");

#ifdef PLF_64BIT_SUPPORT
  // Values that the 32-bit _BitScan fallback cabin.toml records gets right.
  check(plf::countr_zero((1ULL << 40) | (1ULL << 20)) == 20 &&
            plf::countl_zero(1ULL << 20) == 43 && plf::popcount(~0ULL) == 64,
        "64-bit bit tools");

  // bits_to_chars skips zero words, so the output starts as zero characters.
  const unsigned char bytes[] = {0xA5, 0x00, 0x01};
  std::string low_first(20, '0');
  plf::bits_to_chars<false>(bytes, &low_first[0], '0', '1', low_first.size());
  check(low_first == "10100101000000001000", "bits_to_chars, LSB first");
  std::string high_first(20, '0');
  plf::bits_to_chars<true>(bytes, &high_first[0], '0', '1', high_first.size());
  check(high_first == "00010000000010100101", "bits_to_chars, MSB first");

  const unsigned long long words[] = {0xF0, 0x5};
  std::string spread(70, '0');
  plf::bits_to_chars<false>(words, &spread[0], '0', '1', spread.size());
  check(spread == "00001111" + std::string(56, '0') + "101000",
        "bits_to_chars across 64-bit words");
#endif
}

void test_uninitialized_tools() {
  const std::string names[] = {"alpha", "beta", "gamma"};
  counting_allocator<std::string> alloc;
  std::string *copies = alloc.allocate(3);
  plf::uninitialized_copy(names, names + 3, copies, alloc);
  check(constructs == 3 && copies[0] == "alpha" && copies[2] == "gamma",
        "plf::uninitialized_copy calls the allocator's construct");
  destroy_all(copies, 3);
  alloc.deallocate(copies, 3);

  std::string *fills = alloc.allocate(4);
  plf::uninitialized_fill_n(fills, 4, std::string("x"), alloc);
  check(constructs == 7 && fills[3] == "x",
        "plf::uninitialized_fill_n calls the allocator's construct");
  destroy_all(fills, 4);
  alloc.deallocate(fills, 4);

  int numbers[] = {7, 8, 9};
  std::allocator<int> plain;
  int *ints = plain.allocate(3);
  plf::uninitialized_copy(numbers, numbers + 3, ints, plain);
  check(ints[0] == 7 && ints[2] == 9, "plf::uninitialized_copy of ints");
  plain.deallocate(ints, 3);

#ifdef PLF_MOVE_SEMANTICS_SUPPORT
  std::string sources[] = {"one", "two"};
  std::allocator<std::string> strings;
  std::string *moved = strings.allocate(2);
  plf::uninitialized_move(sources, sources + 2, moved, strings);
  check(moved[0] == "one" && moved[1] == "two", "plf::uninitialized_move");
  destroy_all(moved, 2);
  strings.deallocate(moved, 2);
#endif
}

}  // namespace

// plf_tools_undef.h clears the compiler defines, and including plf_tools.h
// again, as a second plf header would, restores them.
#include <plf_tools_undef.h>
#if defined(PLF_COMPILER_DEFINES) || defined(PLF_NOEXCEPT) || \
    defined(PLF_CONSTRUCT)
#error "plf_tools_undef.h leaves the compiler defines set"
#endif
#include <plf_tools.h>
#if !defined(PLF_COMPILER_DEFINES) || !defined(PLF_NOEXCEPT)
#error "plf_tools.h does not restore the compiler defines"
#endif

int main() {
  test_tools();
  test_bit_tools();
  test_uninitialized_tools();
  if (failures != 0) {
    return 1;
  }
  std::printf("plf-tools: countl_zero(0x10000u) = %d, %d constructs\n",
              static_cast<int>(plf::countl_zero(0x00010000u)), constructs);
  return 0;
}
