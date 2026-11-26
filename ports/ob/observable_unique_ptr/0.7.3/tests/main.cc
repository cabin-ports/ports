// Drives owner/observer lifetimes for both owner kinds, observer copy and move,
// derived-to-base observers and enable_observer_from_this.
#include <oup/observable_unique_ptr.hpp>

#include <cstdio>
#include <string>
#include <utility>

static int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                \
    }                                                            \
  } while (0)

struct Base {
  virtual ~Base() = default;
  virtual int id() const { return 1; }
};
struct Derived : Base {
  int id() const override { return 2; }
};

struct SelfUnique : oup::enable_observer_from_this_unique<SelfUnique> {
  int v = 5;
};

struct SelfSealed : oup::enable_observer_from_this_sealed<SelfSealed> {
  explicit SelfSealed(control_block_type& block, int x)
      : oup::enable_observer_from_this_sealed<SelfSealed>(block), v(x) {}
  int v;
};

int main() {
  // Sealed owner leaving scope expires the observer.
  oup::observer_ptr<std::string> scoped;
  CHECK(scoped.expired());
  CHECK(scoped == nullptr);
  {
    auto owner = oup::make_observable_sealed<std::string>("hello");
    scoped = owner;
    CHECK(!scoped.expired());
    CHECK(scoped != nullptr);
    CHECK(*scoped == "hello");
    CHECK(scoped->size() == 5);
    CHECK(scoped.get() == owner.get());
  }
  CHECK(scoped.expired());
  CHECK(scoped == nullptr);

  // Unique owner reset expires the observer; a reassigned owner is a new object.
  auto uniq = oup::make_observable_unique<int>(7);
  oup::observer_ptr<int> o1 = uniq;
  CHECK(!o1.expired());
  CHECK(*o1 == 7);
  uniq.reset();
  CHECK(o1.expired());
  CHECK(uniq == nullptr);
  uniq = oup::make_observable_unique<int>(8);
  CHECK(o1.expired());
  oup::observer_ptr<int> o2 = uniq;
  CHECK(!o2.expired() && *o2 == 8);

  // Moving an owner keeps the observer valid; release hands the object back.
  auto moved = std::move(uniq);
  CHECK(uniq == nullptr);
  CHECK(!o2.expired() && *o2 == 8);
  int* raw = moved.release();
  CHECK(raw != nullptr && *raw == 8);
  CHECK(o2.expired());
  delete raw;

  // Observer copy and move: both observers track the owner; a moved-from one is empty.
  oup::observable_unique_ptr<std::string> s = oup::make_observable_unique<std::string>("abc");
  oup::observer_ptr<std::string> a = s;
  oup::observer_ptr<std::string> b = a;
  CHECK(!a.expired() && !b.expired() && *b == "abc");
  oup::observer_ptr<std::string> c = std::move(a);
  CHECK(a.expired());
  CHECK(!c.expired() && *c == "abc");
  oup::observer_ptr<std::string> d;
  d = b;
  CHECK(!d.expired() && b == d);
  oup::observer_ptr<std::string> e;
  e = std::move(c);
  CHECK(c.expired() && !e.expired() && e == d);
  s.reset();
  CHECK(b.expired() && d.expired() && e.expired());

  // Observer of a base class from a derived owner, with virtual dispatch.
  oup::observable_unique_ptr<Derived> downer = oup::make_observable_unique<Derived>();
  oup::observer_ptr<Base> base = downer;
  CHECK(!base.expired());
  CHECK(base->id() == 2);
  oup::observer_ptr<Derived> back = oup::dynamic_pointer_cast<Derived>(base);
  CHECK(!back.expired() && back.get() == downer.get());
  oup::observable_sealed_ptr<Derived> sealed_derived = oup::make_observable_sealed<Derived>();
  oup::observer_ptr<Base> base2 = sealed_derived;
  CHECK(base2->id() == 2);
  downer.reset();
  CHECK(base.expired() && back.expired());
  CHECK(!base2.expired());

  // enable_observer_from_this with a unique owner.
  {
    auto self = oup::make_observable_unique<SelfUnique>();
    oup::observer_ptr<SelfUnique> so = self->observer_from_this();
    CHECK(!so.expired());
    CHECK(so.get() == self.get());
    CHECK(so->v == 5);
    self.reset();
    CHECK(so.expired());
  }

  // enable_observer_from_this with a sealed owner (control block passed to the constructor).
  oup::observer_ptr<SelfSealed> sealed_obs;
  {
    auto self = oup::make_observable_sealed<SelfSealed>(11);
    sealed_obs = self->observer_from_this();
    CHECK(!sealed_obs.expired());
    CHECK(sealed_obs.get() == self.get());
    CHECK(sealed_obs->v == 11);
  }
  CHECK(sealed_obs.expired());

  if (failures == 0) std::printf("ok\n");
  return failures == 0 ? 0 : 1;
}
