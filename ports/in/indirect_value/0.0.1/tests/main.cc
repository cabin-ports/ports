// Consumer test for cabin-ports/indirect_value: in-place construction, deep
// copies, moves, assignment, swap, const propagation, the pointer constructor
// with its deduction guide, and a custom copier and deleter.  The cases follow
// upstream's test_indirect_value.cpp.
#include <indirect_value.h>

#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

using isocpp_p1950::indirect_value;

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void test_value_semantics() {
  check(sizeof(indirect_value<int>) == sizeof(std::unique_ptr<int>), "the default copier takes no space");

  indirect_value<int> a{std::in_place, 5};
  check(static_cast<bool>(a) && *a == 5, "in-place construction");

  // The copy and move constructors are explicit, so these are direct-initialised.
  indirect_value<int> b(a);
  check(*b == 5 && a.operator->() != b.operator->(), "a copy owns a new object");
  *b = 6;
  check(*a == 5 && *b == 6, "copies are independent");

  int *where = a.operator->();
  indirect_value<int> c(std::move(a));
  check(*c == 5 && c.operator->() == where && !a, "a move transfers the object");

  indirect_value<int> d;
  check(!d, "default construction is empty");
  d = c;
  check(*d == 5 && d.operator->() != c.operator->(), "copy assignment copies the object");
  where = b.operator->();
  d = std::move(b);
  check(*d == 6 && d.operator->() == where && !b, "move assignment transfers the object");

  swap(c, d);
  check(*c == 6 && *d == 5, "swap exchanges the objects");

  indirect_value p(new int(7));
  check((std::is_same<decltype(p), indirect_value<int>>::value) && *p == 7, "the pointer constructor and deduction guide");

  const indirect_value<std::string> s{std::in_place, "abc"};
  check((std::is_same<decltype(*s), const std::string &>::value) && s->size() == 3, "constness propagates to the object");
}

static int copies = 0, deletes = 0;

struct counting_copy {
  int *operator()(const int &v) const {
    ++copies;
    return new int(v);
  }
};

struct counting_delete {
  void operator()(int *p) const {
    ++deletes;
    delete p;
  }
};

static void test_custom_copier_and_deleter() {
  typedef indirect_value<int, counting_copy, counting_delete> counted;
  {
    counted a(new int(1));
    counted b(a);
    check(copies == 1 && deletes == 0 && *b == 1, "copying calls the copier once");
    a = counted();
    check(!a && deletes == 1, "replacing a value deletes the old object");
  }
  check(copies == 1 && deletes == 2, "destruction deletes each owned object once");
}

int main() {
  test_value_semantics();
  test_custom_copier_and_deleter();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("indirect_value 0.0.1 C++ ok");
  return 0;
}
