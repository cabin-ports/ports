// The C++ consumer: function2 in C++14.
#include <function2/function2.hpp>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::fprintf(stderr, "main.cc:%d: check failed: %s\n", __LINE__, \
                   #cond);                                            \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

namespace {

struct Counter {
  int calls = 0;
  int operator()() { return ++calls; }
};

// Callable only as an rvalue: the call gives its string away.
struct TakeOnce {
  std::string s;
  std::string operator()() && { return std::move(s); }
};

// Counts the allocations function2 makes through it.
template <typename T>
struct CountingAllocator {
  using value_type = T;
  int* count;
  explicit CountingAllocator(int* c) : count(c) {}
  template <typename U>
  CountingAllocator(const CountingAllocator<U>& other) : count(other.count) {}
  T* allocate(std::size_t n) {
    ++*count;
    return std::allocator<T>().allocate(n);
  }
  void deallocate(T* p, std::size_t n) {
    std::allocator<T>().deallocate(p, n);
  }
};

// A signature's qualifiers decide which callables the wrapper accepts.
static_assert(
    !std::is_constructible<fu2::function<int() const>, Counter>::value,
    "a const signature rejects a callable with a non-const call");
static_assert(
    !std::is_constructible<fu2::unique_function<std::string()>,
                           TakeOnce>::value,
    "an unqualified signature rejects an rvalue-only callable");

void copyable_functions() {
  int offset = 10;
  fu2::function<int(int) const> add = [offset](int x) { return x + offset; };
  const auto& const_add = add;
  CHECK(add && const_add(1) == 11);

  // Each copy owns a clone of the mutable state.
  fu2::function<int()> next = [n = 0]() mutable { return ++n; };
  CHECK(next() == 1);
  fu2::function<int()> copy = next;
  CHECK(copy() == 2 && copy() == 3 && next() == 2);

  // One wrapper, two call signatures, served by one generic lambda.
  fu2::function<std::size_t(const std::vector<int>&),
                std::size_t(const std::string&) const>
      size = [](const auto& container) { return container.size(); };
  CHECK(size(std::vector<int>{1, 2, 3}) == 3 && size(std::string("ab")) == 2);

  fu2::function<int()> one = [] { return 1; };
  fu2::function<int()> two = [] { return 2; };
  one.swap(two);
  CHECK(one() == 2 && two() == 1);
  one = two;
  CHECK(one() == 1 && two() == 1);

  add = nullptr;
  CHECK(add.empty() && !add);
}

void unique_and_rvalue_functions() {
  fu2::unique_function<int() const> owner = [p = std::make_unique<int>(7)] {
    return *p;
  };
  fu2::unique_function<int() const> moved = std::move(owner);
  CHECK(moved && moved() == 7);

  // A copyable function converts into a unique one.
  fu2::function<int()> copyable = [] { return 4; };
  fu2::unique_function<int()> converted = copyable;
  CHECK(converted() == 4 && copyable() == 4);

  // An rvalue-qualified signature invokes its callable as an rvalue.
  fu2::unique_function<std::string() &&> take = TakeOnce{"once"};
  CHECK(std::move(take)() == "once");
}

void views_and_std_function() {
  Counter counter;
  fu2::function_view<int()> view(counter);
  CHECK(view() == 1 && view() == 2 && counter.calls == 2);  // not a copy

  std::function<int(int)> doubled = [](int x) { return 2 * x; };
  fu2::function<int(int)> from_std = doubled;
  std::function<int(int)> back = from_std;
  CHECK(from_std(4) == 8 && back(5) == 10);

  // An empty std::function gives an empty wrapper, not one that holds it.
  std::function<int(int)> none;
  fu2::function<int(int)> from_none = none;
  CHECK(!from_none);
}

void storage_and_overload() {
  // The default capacity keeps a small callable, here boxed with its
  // allocator, in place.  fu2::capacity_none leaves only pointer-sized
  // storage, so the same box is allocated, and so is each copy, which
  // owns a clone of the state.
  int allocations = 0;
  fu2::function<int()> in_place;
  in_place.assign([n = 0]() mutable { return ++n; },
                  CountingAllocator<char>(&allocations));
  CHECK(in_place() == 1 && allocations == 0);

  using Unbuffered = fu2::function_base<true, true, fu2::capacity_none, true,
                                        false, int()>;
  Unbuffered boxed;
  boxed.assign([n = 0]() mutable { return ++n; },
               CountingAllocator<char>(&allocations));
  CHECK(boxed() == 1 && allocations == 1);
  Unbuffered copy(boxed);
  CHECK(allocations == 2 && copy() == 2 && copy() == 3 && boxed() == 2);

  auto overloaded =
      fu2::overload([](int x) { return x * 3; },
                    [](const char* s) { return s[0] == 'x' ? 1 : 0; });
  CHECK(overloaded(5) == 15 && overloaded("xyz") == 1);
}

void empty_call_throws() {
  fu2::function<void()> empty;
  CHECK(!empty);
  bool threw = false;
  try {
    empty();
  } catch (const fu2::bad_function_call&) {
    threw = true;
  }
  CHECK(threw);

  // Exceptions from the callable itself pass through unchanged.
  fu2::function<void()> thrower = [] { throw std::runtime_error("inner"); };
  std::string what;
  try {
    thrower();
  } catch (const std::runtime_error& e) {
    what = e.what();
  }
  CHECK(what == "inner");
}

}  // namespace

int main() {
  copyable_functions();
  unique_and_rvalue_functions();
  views_and_std_function();
  storage_and_overload();
  empty_call_throws();
  if (failures != 0) return 1;
  std::puts("function2 C++ consumer test passed");
  return 0;
}
