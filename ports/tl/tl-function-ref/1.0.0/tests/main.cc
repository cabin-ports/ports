// tl::function_ref: a non-owning reference that calls the callable it was
// bound to, checked with exact results for each kind of callable.
#include <tl/function_ref.hpp>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

int apply(tl::function_ref<int(int)> f, int x) { return f(x); }

int triple(int x) { return 3 * x; }

struct Account {
  int balance;
  int with(int x) const { return balance + x; }
  int deposit(int x) {
    balance += x;
    return balance;
  }
};

struct Scale {
  int factor;
  int operator()(int x) const { return factor * x; }
};

// Reports which overload a call reached.
struct Which {
  int operator()(int) & { return 1; }
  int operator()(int) const & { return 2; }
};

}  // namespace

int main() {
  // A lambda is called in place: its own mutable state advances.
  auto counter = [n = 0](int x) mutable { return x + ++n; };
  CHECK(apply(counter, 10) == 11 && apply(counter, 10) == 12 &&
        counter(10) == 13);

  // A void signature over a capturing lambda.
  int total = 0;
  auto accumulate = [&total](int x) { total += x; };
  tl::function_ref<void(int)> sink = accumulate;
  sink(3);
  sink(4);
  CHECK(total == 7);

  // A free function, and a function pointer.
  CHECK(apply(triple, 4) == 12);
  int (*fp)(int) = triple;
  CHECK(apply(fp, -2) == -6);

  // Member function pointers take the object as the first argument.
  Account acct{100};
  auto with = &Account::with;
  tl::function_ref<int(const Account&, int)> peek = with;
  CHECK(peek(acct, 5) == 105 && acct.balance == 100);
  auto deposit = &Account::deposit;
  tl::function_ref<int(Account&, int)> add = deposit;
  CHECK(add(acct, 5) == 105 && add(acct, 1) == 106 && acct.balance == 106);
  tl::function_ref<int(Account*, int)> add_via_pointer = deposit;
  CHECK(add_via_pointer(&acct, 4) == 110 && acct.balance == 110);

  // A const callable, and the cv-qualification a call is made with.
  const Scale twice{2};
  CHECK(apply(twice, 21) == 42);
  Which w;
  const Which& cw = w;
  CHECK(apply(w, 0) == 1 && apply(cw, 0) == 2);

  // swap exchanges targets; operator= rebinds.
  tl::function_ref<int(int)> a = triple;
  tl::function_ref<int(int)> b = twice;
  swap(a, b);
  CHECK(a(5) == 10 && b(5) == 15);
  a.swap(b);
  CHECK(a(5) == 15 && b(5) == 10);
  a = counter;
  CHECK(a(0) == 4 && counter(0) == 5);
  // Assigning a const function_ref copies its target, which stays after the
  // source is rebound (a non-const source does not: see cabin.toml).
  const tl::function_ref<int(int)>& source = a;
  b = source;
  a = triple;
  CHECK(b(0) == 6 && a(1) == 3);

  if (failures != 0) {
    return 1;
  }
  std::printf("tl-function-ref ok\n");
  return 0;
}
