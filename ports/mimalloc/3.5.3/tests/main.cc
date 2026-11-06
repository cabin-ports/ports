// Consumer test from C++: linking mimalloc leaves the C++ runtime's
// operator new alone, including its new_handler protocol, which the C
// build's weak std::get_new_handler fallback must not take over (see the
// patch); standard containers on mi_stl_allocator; and a heap whose STL
// allocator releases everything when the heap is destroyed.
#include <mimalloc.h>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

int handler_calls = 0;

void on_exhaustion() {
  ++handler_calls;
  std::set_new_handler(nullptr);  // the next attempt throws std::bad_alloc
}

}  // namespace

int main() {
  // new is the C++ runtime's, not mimalloc's, and a failing ::operator new
  // calls the program's new_handler before it throws.  (In the GCC/Clang
  // C build, the program's own std::get_new_handler() would answer NULL: it
  // links mimalloc's hidden fallback, as with upstream's static library.)
  std::unique_ptr<int[]> numbers(new int[1000]());
  CHECK(!mi_is_in_heap_region(numbers.get()));
  std::set_new_handler(on_exhaustion);
  volatile std::size_t huge = std::numeric_limits<std::ptrdiff_t>::max();
  bool threw = false;
  try {
    void* volatile block = ::operator new(huge);
    ::operator delete(block);
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  CHECK(threw && handler_calls == 1);

  // mimalloc's own failure path consults the new_handler when the library
  // is compiled as C++, as with MSVC (see cabin.toml); the GCC/Clang C
  // build finds its hidden fallback instead.
  std::set_new_handler(on_exhaustion);
  handler_calls = 0;
  CHECK(mi_new_nothrow(huge) == nullptr);
#if defined(_MSC_VER)
  CHECK(handler_calls == 1);
#endif
  std::set_new_handler(nullptr);

  // Containers on mi_stl_allocator.
  std::map<int, std::string, std::less<int>,
           mi_stl_allocator<std::pair<const int, std::string>>>
      names;
  for (int i = 0; i < 100; ++i) names[i] = std::to_string(i * i);
  CHECK(names.size() == 100 && names[12] == "144");
  std::vector<double, mi_stl_allocator<double>> values(4096, 0.5);
  CHECK(mi_is_in_heap_region(values.data()) && values[4095] == 0.5);

  // A vector in a heap of its own; destroying the heap frees its storage
  // without the vector's deallocate (mi_heap_destroy_stl_allocator does
  // nothing on deallocate).
  {
    mi_heap_destroy_stl_allocator<int> alloc;
    std::vector<int, mi_heap_destroy_stl_allocator<int>> squares(alloc);
    for (int i = 0; i < 1000; ++i) squares.push_back(i * i);
    CHECK(squares[999] == 998001);
    CHECK(mi_is_in_heap_region(squares.data()));
    CHECK(!mi_heap_contains(mi_heap_main(), squares.data()));
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
