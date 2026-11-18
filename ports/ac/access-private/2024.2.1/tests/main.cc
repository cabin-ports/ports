#include <access_private.hpp>

#include <cstdio>

class A {
  int m_i = 3;
  int m_f(int p) { return 14 * p; }
  int m_g(int x) { return x + m_i; }
  int m_g(int x, int y) { return x + y * m_i; }
  constexpr int m_c(int p) const { return p * m_i; }
  static int s_i;
  static int s_f(int r) { return r + s_i; }
  static constexpr int s_c(int r) { return r * 2; }
};

int A::s_i = 404;

// The macros expand to namespace-scope declarations, so they stay global.
ACCESS_PRIVATE_FIELD(A, int, m_i)
ACCESS_PRIVATE_FUN(A, int(int), m_f)
ACCESS_PRIVATE_FUN(A, int(int), m_g)
ACCESS_PRIVATE_FUN(A, int(int, int), m_g)
ACCESS_PRIVATE_STATIC_FIELD(A, int, s_i)
ACCESS_PRIVATE_STATIC_FUN(A, int(int), s_f)
ACCESS_PRIVATE_STATIC_FUN(A, int(int), s_c)
using const_A = const A;
ACCESS_PRIVATE_FUN(const_A, int(int) const, m_c)

constexpr A constant_a{};
static_assert(access_private::m_i(constant_a) == 3, "constexpr field access");
static_assert(call_private::m_c(constant_a, 5) == 15, "constexpr member call");
static_assert(call_private_static::A::s_c(5) == 10, "constexpr static call");

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

} // namespace

int main() {
  A a;
  CHECK(access_private::m_i(a) == 3);
  CHECK(access_private::m_i(A()) == 3);
  access_private::m_i(a) = 7;
  const A &const_a = a;
  CHECK(access_private::m_i(const_a) == 7);
  CHECK(&access_private::m_i(const_a) == &access_private::m_i(a));

  CHECK(call_private::m_f(a, 3) == 42);
  CHECK(call_private::m_f(A(), 3) == 42);
  // m_g reads m_i, so these also observe the write above.
  CHECK(call_private::m_g(a, 1) == 8);
  CHECK(call_private::m_g(a, 1, 2) == 15);
  CHECK(call_private::m_g(A(), 1, 2) == 7);

  CHECK(access_private_static::A::s_i() == 404);
  access_private_static::A::s_i() = 405;
  CHECK(access_private_static::A::s_i() == 405);
  // s_f reads s_i, so this also observes the write above.
  CHECK(call_private_static::A::s_f(4) == 409);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("access-private ok");
  return 0;
}
