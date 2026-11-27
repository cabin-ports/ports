#include <duals/dual>

#include <cmath>
#include <cstdio>
#include <streambuf>
#include <string>

using duals::dual;

static int failures = 0;

// A string-collecting stream without <sstream>: operator<< uses
// std::basic_ostringstream, which <duals/dual> does not include itself.
struct StringBuf : std::streambuf {
  std::string str;
  int_type overflow(int_type c) override {
    if (c != traits_type::eof()) str.push_back(static_cast<char>(c));
    return c;
  }
};

static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-12; }

// f(x) = 3x^3 - 2x + 5 on a dual seeded with dx = 1.
template <class T> static T poly(T x) { return 3 * x * x * x - 2 * x + 5; }

int main() {
  // Arithmetic: (a+b e)(c+d e) = ac + (ad+bc) e, and the quotient rule.
  dual<double> a(2, 3), b(5, 7);
  check(near((a + b).rpart(), 7) && near((a + b).dpart(), 10), "sum");
  check(near((a * b).rpart(), 10) && near((a * b).dpart(), 29), "product");
  check(near((a / b).rpart(), 0.4) && near((a / b).dpart(), (3 * 5 - 2 * 7) / 25.0),
        "quotient");

  // Derivatives against analytic values.
  dual<double> x(2, 1);
  dual<double> p = poly(x);
  check(near(p.rpart(), 25) && near(p.dpart(), 34), "polynomial");
  dual<double> s = sin(x);
  check(near(s.rpart(), std::sin(2.0)) && near(s.dpart(), std::cos(2.0)), "sin");
  dual<double> e = exp(x);
  check(near(e.rpart(), std::exp(2.0)) && near(e.dpart(), std::exp(2.0)), "exp");
  dual<double> w = pow(x, 3);
  check(near(w.rpart(), 8) && near(w.dpart(), 12), "pow");

  dual<double> er = erf(dual<double>(0, 1));
  check(near(er.rpart(), 0) && near(er.dpart(), 1.1283791670955126), "erf");

  // Nested duals give the second derivative: f''(2) = 18 * 2 = 36.
  dual<dual<double>> xx(dual<double>(2, 1), dual<double>(1, 0));
  dual<dual<double>> pp = poly(xx);
  check(near(pp.rpart().rpart(), 25) && near(pp.rpart().dpart(), 34) &&
            near(pp.dpart().dpart(), 36),
        "nested second derivative");

  // Two partial derivatives at once: f(x, y) = x^2 y at (3, 4).
  dual<double, 2> fx(3, {1, 0}), fy(4, {0, 1});
  dual<double, 2> f = fx * fx * fy;
  check(near(f.rpart(), 36) && near(f.dpart(0), 24) && near(f.dpart(1), 9),
        "two partials");

  // Comparisons look at the real part only.
  check(dual<double>(1, 9) < dual<double>(2, 0) && dual<double>(1, 2) == dual<double>(1, 3) &&
            dual<double>(2, 0) != dual<double>(1, 0) && dual<double>(2, 0) >= 2.0,
        "comparison");

  // Stream output.
  StringBuf buf;
  std::ostream os(&buf);
  os << dual<double>(1, 2) << ' ' << dual<double>(1, -2);
  check(buf.str == "(1+2_e) (1-2_e)", "stream output");

  if (failures == 0) std::printf("all checks passed\n");
  return failures == 0 ? 0 : 1;
}
