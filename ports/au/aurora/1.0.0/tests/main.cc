#include <Aurora/Dispatch.hpp>
#include <Aurora/Meta.hpp>
#include <Aurora/SmartPtr.hpp>
#include <Aurora/Tools/Hash.hpp>
#include <Aurora/Tools/Optional.hpp>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>

static int failures = 0;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
      ++failures;                                           \
    }                                                       \
  } while (0)

struct Base {
  virtual ~Base() {}
  virtual int f() const = 0;
};
struct D : Base {
  int v;
  explicit D(int x) : v(x) {}
  int f() const override { return v; }
};
struct E : Base {
  int f() const override { return 0; }
};

static int twice(D* d) { return d->v * 2; }

static_assert(AURORA_PP_SIZE((a, b, c)) == 3, "size of 3-tuple");
static_assert(AURORA_PP_SIZE(()) == 0, "size of empty tuple");

int main() {
  // Deep copy of a CopiedPtr.
  auto a = aurora::makeCopied<int>(42);
  aurora::CopiedPtr<int> b = a;
  *b = 7;
  CHECK(*a == 42);
  CHECK(*b == 7);
  CHECK(a.get() != b.get());

  // Copy through a base pointer keeps the dynamic type.
  aurora::CopiedPtr<Base> p = aurora::makeCopied<D>(5);
  aurora::CopiedPtr<Base> q = p;
  CHECK(q->f() == 5);
  CHECK(q.get() != p.get());
  CHECK(dynamic_cast<D*>(q.get()) != nullptr);

  // Optional is checked only empty: it asserts when given a value (see cabin.toml).
  aurora::Optional<std::string> o;
  CHECK(!o);
  aurora::Optional<int> n = aurora::nullopt;
  CHECK(!n);

  aurora::SingleDispatcher<int(Base*)> disp;
  disp.bind(aurora::Type<D>(), &twice);
  D d21(21);
  CHECK(disp.call(&d21) == 42);
  E e;
  bool thrown = false;
  try {
    disp.call(&e);
  } catch (const aurora::FunctionCallException&) {
    thrown = true;
  }
  CHECK(thrown);

  // hashCombine follows Boost's formula for seed 0.
  std::size_t seed = 0;
  aurora::hashCombine(seed, 1);
  CHECK(seed == std::hash<int>()(1) + 0x9e3779b9u);

  return failures == 0 ? 0 : 1;
}
