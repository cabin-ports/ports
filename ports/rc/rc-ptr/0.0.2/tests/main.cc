#include <rc_ptr/rc_ptr.hpp>

#include <cstdio>
#include <memory>
#include <utility>

namespace {

int failures = 0;
int destroyed = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,         \
                   __LINE__, #cond);                                      \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

struct Tracked {
  int value;
  explicit Tracked(int v) : value(v) {}
  ~Tracked() { ++destroyed; }
};

struct Node : memory::enable_rc_from_this<Node> {
  int id;
  Node() : id(7) {}
};

}  // namespace

int main() {
  // Construction, copies, moves and use_count.
  {
    memory::rc_ptr<Tracked> a{new Tracked{24}};
    CHECK(a.use_count() == 1);
    CHECK(a.unique());
    CHECK(static_cast<bool>(a));
    CHECK(a->value == 24);

    memory::rc_ptr<Tracked> b = a;
    CHECK(a.use_count() == 2);
    CHECK(b.use_count() == 2);
    CHECK(!a.unique());
    CHECK(a.get() == b.get());
    CHECK((*b).value == 24);

    memory::rc_ptr<Tracked> c = std::move(b);
    CHECK(!b);
    CHECK(b.use_count() == 0);
    CHECK(a.use_count() == 2);
    CHECK(c.get() == a.get());
    CHECK(destroyed == 0);
  }
  // The pointee is destroyed exactly once, when the last owner goes.
  CHECK(destroyed == 1);

  // Empty pointers.
  {
    memory::rc_ptr<int> e;
    memory::rc_ptr<int> n{nullptr};
    CHECK(!e && !n);
    CHECK(e.use_count() == 0);
    CHECK(e.get() == nullptr);
  }

  // make_rc forwards its arguments to the pointee's constructor.
  destroyed = 0;
  {
    auto m = memory::make_rc<Tracked>(10);
    CHECK(m->value == 10);
    CHECK(m.use_count() == 1);
  }
  CHECK(destroyed == 1);

  // reset and swap.
  destroyed = 0;
  {
    memory::rc_ptr<Tracked> x{new Tracked{1}};
    memory::rc_ptr<Tracked> y{new Tracked{2}};
    memory::rc_ptr<Tracked> y2 = y;
    x.swap(y);
    CHECK(x->value == 2 && x.use_count() == 2);
    CHECK(y->value == 1 && y.use_count() == 1);
    CHECK(destroyed == 0);
    y.reset();
    CHECK(!y);
    CHECK(destroyed == 1);
    x.reset();
    CHECK(x.use_count() == 0);
    CHECK(y2.use_count() == 1);
    CHECK(destroyed == 1);
  }
  CHECK(destroyed == 2);

  // Custom deleter, adopting a std::unique_ptr, and get_deleter.
  {
    int deleted = 0;
    auto deleter = [&deleted](int* p) {
      ++deleted;
      delete p;
    };
    {
      memory::rc_ptr<int, decltype(deleter)> d{new int{5}, deleter};
      auto d2 = d;
      CHECK(*d2 == 5);
      CHECK(d.use_count() == 2);
    }
    CHECK(deleted == 1);

    std::unique_ptr<Tracked> u{new Tracked{3}};
    destroyed = 0;
    {
      memory::rc_ptr<Tracked> from_unique{std::move(u)};
      CHECK(!u);
      CHECK(from_unique->value == 3);
    }
    CHECK(destroyed == 1);
  }

  // weak_rc_ptr: lock, expired, use_count, and the bad_weak_rc_ptr exception.
  destroyed = 0;
  {
    memory::weak_rc_ptr<Tracked> w;
    CHECK(w.expired());
    {
      memory::rc_ptr<Tracked> s{new Tracked{9}};
      w = s;
      CHECK(!w.expired());
      CHECK(w.use_count() == 1);
      CHECK(s.use_count() == 1);  // a weak reference adds no owner
      auto locked = w.lock();
      CHECK(locked->value == 9);
      CHECK(s.use_count() == 2);
    }
    CHECK(destroyed == 1);
    CHECK(w.expired());
    CHECK(w.use_count() == 0);
    CHECK(!w.lock());
    bool threw = false;
    try {
      memory::rc_ptr<Tracked> bad{w};
    } catch (const memory::bad_weak_rc_ptr&) {
      threw = true;
    }
    CHECK(threw);
  }

  // owner_before and owner_less order by control block.
  {
    memory::rc_ptr<int> p{new int{1}};
    memory::rc_ptr<int> q{new int{2}};
    auto p2 = p;
    memory::owner_less<memory::rc_ptr<int>> less;
    CHECK(!less(p, p2) && !less(p2, p));
    CHECK(less(p, q) != less(q, p));
  }

  // enable_rc_from_this shares ownership with the creating rc_ptr.
  {
    memory::rc_ptr<Node> n{new Node{}};
    auto again = n->rc_from_this();
    CHECK(again.get() == n.get());
    CHECK(n.use_count() == 2);
    CHECK(n->weak_rc_from_this().use_count() == 2);
    CHECK(again->id == 7);
  }

  // Array form.
  {
    memory::rc_ptr<int[]> arr{new int[3]{4, 5, 6}};
    CHECK(arr[0] == 4 && arr[2] == 6);
  }

  return failures == 0 ? 0 : 1;
}
