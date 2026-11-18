// Consumer test for cabin-ports/intrusive_shared_ptr: the ref_counted base
// with make_refcnt, refcnt_attach and refcnt_retain, weak references, a type
// with its own refcnt_ptr_traits, the ref_counted_adapter, and std::atomic of
// a pointer.  The cases follow the README's usage section.
#include <intrusive_shared_ptr/ref_counted.h>
#include <intrusive_shared_ptr/refcnt_ptr.h>

#include <atomic>
#include <cstdio>
#include <map>
#include <string>

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static int alive = 0;

class foo : public isptr::ref_counted<foo> {
  friend ref_counted;

public:
  foo() { ++alive; }
  int value = 42;

private:
  ~foo() noexcept { --alive; }
};

static void test_ref_counted() {
  auto p1 = isptr::make_refcnt<foo>();
  check(alive == 1 && p1 && p1->value == 42, "make_refcnt creates one object");

  isptr::refcnt_ptr<foo> p2 = p1;
  check(p1.get() == p2.get() && p1 == p2, "copies share the object");
  p1.reset();
  check(!p1 && p1 == nullptr && alive == 1, "the object outlives one reset");

  isptr::refcnt_ptr<foo> p3 = isptr::refcnt_retain(p2.get());
  p2.reset();
  check(alive == 1 && p3->value == 42, "refcnt_retain adds a reference");

  foo *raw = p3.release();
  check(!p3 && alive == 1, "release keeps the reference");
  isptr::refcnt_ptr<foo> p4 = isptr::refcnt_attach(raw);
  p4.reset();
  check(alive == 0, "refcnt_attach adopts the reference, and the last reset deletes");
}

class node : public isptr::weak_ref_counted<node> {
public:
  int id = 7;
};

static void test_weak_references() {
  auto strong = isptr::make_refcnt<node>();
  node::weak_ptr weak = strong->get_weak_ptr();
  isptr::refcnt_ptr<node> locked = weak->lock();
  check(locked == strong && locked->id == 7, "lock returns the live object");
  locked.reset();
  strong.reset();
  check(!weak->lock(), "lock returns null once the object is gone");
}

struct counted {
  int refs = 1;
  struct refcnt_ptr_traits {
    static void add_ref(counted *p) noexcept { ++p->refs; }
    static void sub_ref(counted *p) noexcept { --p->refs; }
  };
};

static void test_custom_traits() {
  counted c;
  {
    isptr::refcnt_ptr<counted> p = isptr::refcnt_retain(&c);
    check(c.refs == 2, "refcnt_retain calls add_ref");
    isptr::refcnt_ptr<counted> q = p;
    check(c.refs == 3, "a copy calls add_ref");
  }
  check(c.refs == 1, "each destroyed pointer calls sub_ref");
}

static void test_adapter_and_atomic() {
  using counted_map = isptr::ref_counted_adapter<std::map<std::string, int>>;
  auto m = isptr::make_refcnt<counted_map>();
  (*m)["abc"] = 7;
  check(m->size() == 1 && m->at("abc") == 7, "ref_counted_adapter wraps a map");

  std::atomic<isptr::refcnt_ptr<counted_map>> shared(m);
  check(shared.load() == m, "atomic load");
  auto other = isptr::make_refcnt<counted_map>();
  isptr::refcnt_ptr<counted_map> old = shared.exchange(other);
  check(old == m && shared.load() == other, "atomic exchange");
}

int main() {
  test_ref_counted();
  test_weak_references();
  test_custom_traits();
  test_adapter_and_atomic();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("intrusive_shared_ptr 1.13.0 C++ ok");
  return 0;
}
