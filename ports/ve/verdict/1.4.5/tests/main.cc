#include <verdict.h>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(double got, double want, const char* what) {
  if (!(std::fabs(got - want) < 1e-5)) {
    std::fprintf(stderr, "FAIL: %s: got %.10g, want %.10g\n", what, got, want);
    ++failures;
  }
}

}  // namespace

int main() {
  // The default configuration verdict_config.h fills in: version 1.4.5,
  // static, unmangled.
  if (VERDICT_VERSION != 145) {
    std::fprintf(stderr, "FAIL: VERDICT_VERSION\n");
    ++failures;
  }

  const double edge[2][3] = {{0, 0, 0}, {3, 4, 0}};
  check(verdict::edge_length(2, edge), 5, "edge_length");

  const double tri[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  check(verdict::tri_area(3, tri), 0.5, "tri_area");

  const double tet[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  check(verdict::tet_volume(4, tet), 1.0 / 6, "tet_volume");

  const double hex[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                            {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  check(verdict::hex_volume(8, hex), 1, "hex_volume");
  check(verdict::hex_scaled_jacobian(8, hex), 1, "hex_scaled_jacobian");

  // The values of upstream's unittests/verdict.test.cpp wedge_simple, at a
  // plain 1e-5 tolerance.
  const double wedge[6][3] = {{0, 0, 0},  {-1, 1, 0}, {-1, 0, 0},
                              {0, 0, 1},  {-1, 1, 1}, {-1, 0, 1}};
  check(verdict::wedge_volume(6, wedge), 0.5, "wedge_volume");
  check(verdict::wedge_equiangle_skew(6, wedge), 0.25, "wedge_equiangle_skew");
  check(verdict::wedge_edge_ratio(6, wedge), std::sqrt(2.), "wedge_edge_ratio");
  check(verdict::wedge_max_aspect_frobenius(6, wedge), 1.1357042248,
        "wedge_max_aspect_frobenius");
  check(verdict::wedge_mean_aspect_frobenius(6, wedge), 1.0978474174,
        "wedge_mean_aspect_frobenius");
  check(verdict::wedge_distortion(6, wedge), 1, "wedge_distortion");
  check(verdict::wedge_max_stretch(6, wedge), 1, "wedge_max_stretch");
  check(verdict::wedge_scaled_jacobian(6, wedge), 0.81649658,
        "wedge_scaled_jacobian");
  check(verdict::wedge_shape(6, wedge), 0.8254818, "wedge_shape");
  check(verdict::wedge_condition(6, wedge), 1.1357042248, "wedge_condition");

  // The singular wedge, upstream's wedge_singularity.
  const double flat[6][3] = {};
  check(verdict::wedge_volume(6, flat), 0, "singular wedge_volume");
  check(verdict::wedge_edge_ratio(6, flat), verdict::VERDICT_DBL_MAX,
        "singular wedge_edge_ratio");

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
