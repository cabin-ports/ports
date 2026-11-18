#include <indirect.h>
#include <polymorphic.h>

#include <compare>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

struct counts {
  int allocations = 0;
  int deallocations = 0;
};

// Counts what the value types allocate through it.
template <class T> struct counting_allocator {
  using value_type = T;
  counts *c;
  explicit counting_allocator(counts *c) noexcept : c(c) {}
  template <class U>
  counting_allocator(const counting_allocator<U> &other) noexcept
      : c(other.c) {}
  T *allocate(std::size_t n) {
    ++c->allocations;
    return std::allocator<T>{}.allocate(n);
  }
  void deallocate(T *p, std::size_t n) noexcept {
    ++c->deallocations;
    std::allocator<T>{}.deallocate(p, n);
  }
};

template <class T, class U>
bool operator==(const counting_allocator<T> &a,
                const counting_allocator<U> &b) noexcept {
  return a.c == b.c;
}

int live = 0;
int rect_moves = 0;

struct shape {
  shape() { ++live; }
  shape(const shape &) { ++live; }
  virtual ~shape() { --live; }
  virtual int area() const = 0;
  virtual void scale(int k) = 0;
};

struct square : shape {
  int side;
  explicit square(int s) : side(s) {}
  int area() const override { return side * side; }
  void scale(int k) override { side *= k; }
};

struct rect : shape {
  int w, h;
  rect(int w, int h) : w(w), h(h) {}
  rect(const rect &) = default;
  rect(rect &&other) noexcept : shape(other), w(other.w), h(other.h) {
    ++rect_moves;
  }
  int area() const override { return w * h; }
  void scale(int k) override {
    w *= k;
    h *= k;
  }
};

void check_indirect() {
  xyz::indirect<int> a(std::in_place, 42);
  CHECK(*a == 42 && !a.valueless_after_move());

  // Copies are deep.
  xyz::indirect<int> b(a);
  *b = 7;
  CHECK(*a == 42 && *b == 7 && &*a != &*b);
  CHECK(a != b && b < a && (a <=> b) == std::strong_ordering::greater);
  CHECK(a == 42 && 7 == b && (b <=> 8) == std::strong_ordering::less);

  // Constness propagates to the owned object.
  const xyz::indirect<int> &view = a;
  static_assert(std::is_same_v<decltype(*view), const int &>);

  xyz::indirect<int> c(std::move(a));
  CHECK(*c == 42 && a.valueless_after_move());
  CHECK(a != c && a < c);
  a = b;
  CHECK(!a.valueless_after_move() && *a == 7 && &*a != &*b);
  swap(a, c);
  CHECK(*a == 42 && *c == 7);
  CHECK(std::hash<xyz::indirect<int>>{}(a) == std::hash<int>{}(42));
  b = a;
  CHECK(*b == 42 && &*a != &*b);
  c = std::move(b);
  CHECK(*c == 42 && b.valueless_after_move());
  CHECK(b != 42 && b < 42);
  c = 1;
  CHECK(*c == 1);
  xyz::indirect<int> e(std::move(c));
  CHECK(*e == 1 && b == c);

  xyz::indirect<std::vector<int>> v(std::in_place, {1, 2, 3});
  v->push_back(4);
  CHECK(v->size() == 4 && (*v)[3] == 4);
  xyz::indirect d(std::string("cabin"));
  static_assert(std::is_same_v<decltype(d), xyz::indirect<std::string>>);
  CHECK(*d == "cabin" && d->size() == 5);
  {
    xyz::indirect<square> s(std::in_place, 4);
    xyz::indirect<square> t(s);
    CHECK(t->area() == 16 && live == 2);
  }

  counts n, m;
  {
    using alloc = counting_allocator<int>;
    xyz::indirect<int, alloc> x(std::allocator_arg, alloc(&n), 5);
    CHECK(n.allocations == 1 && *x == 5);
    xyz::indirect<int, alloc> y(x);
    CHECK(n.allocations == 2 && *y == 5 &&
          y.get_allocator() == x.get_allocator());
    xyz::indirect<int, alloc> z(std::allocator_arg, alloc(&m), std::move(y));
    CHECK(m.allocations == 1 && *z == 5 && y.valueless_after_move());
  }
  CHECK(n.deallocations == 2 && m.deallocations == 1);
}

void check_polymorphic() {
  xyz::polymorphic<shape> p(std::in_place_type<square>, 3);
  CHECK(p->area() == 9 && !p.valueless_after_move());

  // Copies are deep and keep the dynamic type.
  xyz::polymorphic<shape> q(p);
  q->scale(2);
  CHECK(p->area() == 9 && q->area() == 36);
  CHECK(dynamic_cast<const square *>(&*q) != nullptr && &*p != &*q);

  std::vector<xyz::polymorphic<shape>> shapes;
  shapes.emplace_back(std::in_place_type<square>, 2);
  shapes.emplace_back(std::in_place_type<rect>, 2, 5);
  auto copies = shapes;
  copies[1]->scale(10);
  int total = 0;
  for (const auto &s : shapes)
    total += s->area();
  CHECK(total == 14 && copies[1]->area() == 1000);
  CHECK(dynamic_cast<const rect *>(&*copies[1]) != nullptr);

  xyz::polymorphic<shape> r(std::move(p));
  CHECK(r->area() == 9 && p.valueless_after_move());
  swap(q, r);
  CHECK(q->area() == 9 && r->area() == 36);
  p = r;
  CHECK(!p.valueless_after_move() && p->area() == 36);
  q = std::move(r);
  CHECK(q->area() == 36 && r.valueless_after_move());

  counts n, m;
  {
    using alloc = counting_allocator<shape>;
    xyz::polymorphic<shape, alloc> x(std::allocator_arg, alloc(&n),
                                     std::in_place_type<rect>, 3, 4);
    CHECK(n.allocations == 1 && x->area() == 12);
    xyz::polymorphic<shape, alloc> y(x);
    CHECK(n.allocations == 2 && y->area() == 12);
    xyz::polymorphic<shape, alloc> z(std::allocator_arg, alloc(&m),
                                     std::move(y));
    CHECK(m.allocations == 1 && z->area() == 12 &&
          dynamic_cast<const rect *>(&*z) != nullptr);
    // Unequal allocators: the object is moved into a new allocation.
    CHECK(rect_moves == 1);
  }
  CHECK(n.deallocations == 2 && m.deallocations == 1);
}

} // namespace

int main() {
  check_indirect();
  check_polymorphic();
  CHECK(live == 0);
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("jbcoe-value-types ok");
  return 0;
}
