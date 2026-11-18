#include <quaternion.h>
#include <quaternion_io.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                \
    }                                                              \
  } while (0)

using quaternion::Quaternion;
typedef Quaternion<double> Qd;

static bool approx(const Qd& x, const Qd& y) {
  return quaternion::abs(x - y) < 1e-12;
}

template <typename T>
static std::string str(const Quaternion<T>& q) {
  std::ostringstream out;
  out << q;
  return out.str();
}

static void test_arithmetic() {
  const Qd q(1, 2, 3, 4);
  CHECK(q.a() == 1 && q.b() == 2 && q.c() == 3 && q.d() == 4);
  CHECK(quaternion::norm_squared(q) == 30);
  CHECK(quaternion::conj(q) == Qd(1, -2, -3, -4));
  CHECK(q * q == Qd(-28, 4, 6, 8));
  CHECK(q + Qd(1, 1, 1, 1) == Qd(2, 3, 4, 5));
  // i * j == k but j * i == -k.
  const Qd i(0, 1), j(0, 0, 1), k(0, 0, 0, 1);
  CHECK(i * j == k);
  CHECK(j * i == Qd(0, 0, 0, -1));
  CHECK(i * i == Qd(-1));
  CHECK(std::fabs(quaternion::abs(Qd(0, 3, 4)) - 5) < 1e-12);
  CHECK(quaternion::is_unit(Qd(1)));
  CHECK(!quaternion::is_unit(q));
  CHECK(approx(quaternion::inverse(q) * q, Qd(1)));
  CHECK(approx(q / q, Qd(1)));
  // (1 + i)^5 == -4 - 4i, and a negative power inverts.
  CHECK(quaternion::pow(Qd(1, 1), 5) == Qd(-4, -4));
  CHECK(quaternion::pow(q, 3) == q * q * q);
  CHECK(approx(quaternion::pow(q, -3) * (q * q * q), Qd(1)));
  // exp(i * pi / 2) == i.
  const double pi = std::acos(-1.0);
  CHECK(approx(quaternion::exp(Qd(0, pi / 2)), i));
}

static void test_rotation() {
  // A quarter turn about z takes the x axis to the y axis.
  const double h = std::sqrt(0.5);
  const Qd r(h, 0, 0, h);
  CHECK(approx(r * Qd(0, 1, 0, 0) * quaternion::conj(r), Qd(0, 0, 1, 0)));
  const quaternion::rotation_matrix<double> m = quaternion::to_rotation_matrix(r);
  const double expected[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col)
      CHECK(std::fabs(m[row][col] - expected[row][col]) < 1e-12);
  CHECK(approx(quaternion::from_rotation_matrix(m), r));
}

static void test_io() {
  CHECK(str(Quaternion<int>(1)) == "1");
  CHECK(str(Quaternion<int>(0, 1)) == "i");
  CHECK(str(Quaternion<int>(0, 0, 0, -1)) == "-k");
  CHECK(str(Quaternion<int>(1, 2, 3, 4)) == "1+2i+3j+4k");
  CHECK(str(Quaternion<int>(1, -2, 0, 4)) == "1-2i+4k");
  std::ostringstream out;
  out << quaternion::set_display_style(quaternion::q_compact) << Quaternion<int>(1, 2, 3, 4)
      << quaternion::set_display_style(quaternion::q_nice);
  CHECK(out.str() == "{1,2,3,4}");
}

int main() {
  test_arithmetic();
  test_rotation();
  test_io();
  std::puts("quaternions 2024.12.10 ok");
  return 0;
}
