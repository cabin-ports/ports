// gk.hpp first: it includes no <vector> or <cassert> itself.
#include <stmpct/gk.hpp>
#include <stmpct/ckms_tq.hpp>
#include <stmpct/ckms_uq.hpp>

#include <cstdio>
#include <vector>

// Inserting 1..1000 makes each value equal its rank, so a quantile's error in
// rank is its error in value.  The documented guarantee is epsilon * N
// (doc/api_reference/cpp/gk.md:24, ckms_uq.md:25), here 0.01 * 1000 = 10; for
// ckms_tq the target's epsilon is "the desired error" (targeted_quantile.md:20),
// read the same way.
static bool within(double got, double want) {
  return got >= want - 10 && got <= want + 10;
}

static bool check(bool ok, const char* what) {
  if (!ok) std::fprintf(stderr, "FAIL: %s\n", what);
  return ok;
}

int main() {
  const double eps = 0.01;
  bool ok = true;

  stmpct::ckms_uq<double> u(eps);
  stmpct::gk<double> g(eps);
  std::vector<stmpct::targeted_quantile> tqs;
  tqs.emplace_back(0.5, eps);
  stmpct::ckms_tq<double> t(tqs.begin(), tqs.end());
  for (int i = 1; i <= 1000; ++i) {
    u.insert(i);
    g.insert(i);
    t.insert(i);
  }

  ok &= check(within(u.quantile(0.5), 500), "ckms_uq median");
  ok &= check(within(u.quantile(0.99), 990), "ckms_uq p99");
  ok &= check(within(g.quantile(0.5), 500), "gk median");
  ok &= check(within(g.quantile(0.99), 990), "gk p99");
  ok &= check(within(t.quantile(0.5), 500), "ckms_tq targeted median");

  // An empty summary answers T{} (ckms_impl.hpp:27-28).
  stmpct::ckms_uq<double> empty(eps);
  ok &= check(empty.quantile(0.5) == 0.0, "empty ckms_uq");

  return ok ? 0 : 1;
}
