#include <cotila/cotila.h>

#include <cstdio>

using namespace cotila;

constexpr matrix<double, 2, 2> a = {{{1., 3.}, {2., 7.}}};
constexpr matrix<double, 2, 1> x = {{{1.}, {1.}}};

static_assert(matmul(a, x) == matrix<double, 2, 1>{{{4.}, {9.}}}, "matrix-vector");
static_assert(det(a) == 1., "det");
static_assert(det(matrix<double, 2, 2>{{{1., 2.}, {2., 4.}}}) == 0., "singular det");
static_assert(inverse(a) == matrix<double, 2, 2>{{{7., -3.}, {-2., 1.}}}, "inverse");
static_assert(dot(vector{1, 2, 3}, vector{4, 5, 6}) == 32, "dot");
static_assert(transpose(matrix<int, 2, 3>{{{1, 2, 3}, {4, 5, 6}}}) ==
                  matrix<int, 3, 2>{{{1, 4}, {2, 5}, {3, 6}}},
              "transpose");
#if !defined(_MSC_VER)
static_assert(matrix{{{1, 2, 3}, {4, 5, 6}}} ==
                  matrix<int, 2, 3>{{{1, 2, 3}, {4, 5, 6}}},
              "matrix deduction guide");
#endif

int main() {
  // Runtime values, so the same code also runs outside constant evaluation.
  volatile double s = 2.;
  matrix<double, 3, 3> m = {{{2. * s, 0., 0.}, {0., 3., 1.}, {0., 1., 2.}}};
  matrix<double, 3, 1> v = {{{1.}, {2.}, {3.}}};
  auto p = matmul(m, v);
  if (!(p == matrix<double, 3, 1>{{{4.}, {9.}, {8.}}})) return 1;
  if (det(matrix<double, 2, 2>{{{s, 3.}, {1., 2.}}}) != 1.) return 2;
  if (!(matmul(m, inverse(m)) == identity<double, 3>)) return 3;
  if (dot(m.row(1), vector<double, 3>{{0., 2., 3.}}) != 9.) return 4;
  if (rank(m) != 3) return 6;
  if (inverse(a)[0][1] != -3.) return 5;
  std::puts("cotila ok");
  return 0;
}
