// Checks IF97 in its default SI units (Pa, J) against the IAPWS verification
// tables that upstream's IF97.cpp prints in MPa and kJ.
#include <IF97.h>

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void expect_near(const char* what, double got, double want, double rel) {
  const double err = std::fabs(got - want) / std::fabs(want);
  if (!(err <= rel)) {
    std::printf("FAIL %s: got %.10g, want %.10g (rel %.3g > %.3g)\n", what, got,
                want, err, rel);
    ++failures;
  }
}

void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

template <class E, class F>
void throws(const char* what, F f) {
  try {
    f();
  } catch (const E&) {
    return;
  } catch (...) {
  }
  std::printf("FAIL %s: did not throw the expected exception\n", what);
  ++failures;
}

const double MPa = 1e6;
const double kJ = 1e3;

// IAPWS R7-97(2012) Tables 5 (Region 1), 15 (Region 2), 33 (Region 3) and
// 42 (Region 5): T [K], p [MPa], v [m3/kg], h, u [kJ/kg], s, cp [kJ/kg/K],
// w [m/s].
struct Forward {
  const char* region;
  double T, p, v, h, u, s, cp, w;
};

const Forward forward[] = {
    {"1", 300, 3, 0.00100215168, 115.331273, 112.324818, 0.392294792,
     4.17301218, 1507.73921},
    {"1", 300, 80, 0.000971180894, 184.142828, 106.448356, 0.368563852,
     4.01008987, 1634.69054},
    {"1", 500, 3, 0.001202418, 975.542239, 971.934985, 2.58041912, 4.65580682,
     1240.71337},
    {"2", 300, 0.0035, 39.4913866, 2549.91145, 2411.6916, 8.52238967,
     1.91300162, 427.920172},
    {"2", 700, 0.0035, 92.3015898, 3335.68375, 3012.62819, 10.1749996,
     2.08141274, 644.289068},
    {"2", 700, 30, 0.00542946619, 2631.49474, 2468.61076, 5.17540298,
     10.3505092, 480.386523},
    {"3", 650, 25.5837018, 0.002, 1863.43019, 1812.26279, 4.05427273,
     13.8935717, 502.005554},
    {"3", 650, 22.2930643, 0.005, 2375.12401, 2263.65868, 4.85438792,
     44.6579342, 383.444594},
    {"3", 750, 78.3095639, 0.002, 2258.68845, 2102.06932, 4.46971906,
     6.34165359, 760.696041},
    {"5", 1500, 0.5, 1.3845509, 5219.76855, 4527.4931, 9.65408875, 2.61609445,
     917.06869},
    {"5", 1500, 30, 0.0230761299, 5167.23514, 4474.95124, 7.72970133,
     2.72724317, 928.548002},
    {"5", 2000, 30, 0.0311385219, 6571.22604, 5637.07038, 8.53640523,
     2.88569882, 1067.36948},
};

void check_forward() {
  for (const Forward& r : forward) {
    const double p = r.p * MPa;
    // IAPWS tabulates Region 3 at (T, rho); by default IF97 gets rho from
    // the backward v(T,p) equations of IAPWS SR5-05, which are approximate
    // (cp at 650 K and 25.6 MPa is off by 2.2e-5).
    const double rel = r.region[0] == '3' ? 1e-4 : 1e-8;
    std::string name = std::string("region ") + r.region + " ";
    expect_near((name + "rho").c_str(), IF97::rhomass_Tp(r.T, p), 1 / r.v, rel);
    expect_near((name + "h").c_str(), IF97::hmass_Tp(r.T, p), r.h * kJ, rel);
    expect_near((name + "u").c_str(), IF97::umass_Tp(r.T, p), r.u * kJ, rel);
    expect_near((name + "s").c_str(), IF97::smass_Tp(r.T, p), r.s * kJ, rel);
    expect_near((name + "cp").c_str(), IF97::cpmass_Tp(r.T, p), r.cp * kJ, rel);
    expect_near((name + "w").c_str(), IF97::speed_sound_Tp(r.T, p), r.w, rel);
  }
}

void check_saturation() {
  // IAPWS R7-97(2012) Tables 35 and 36.
  expect_near("psat 300 K", IF97::psat97(300), 0.353658941e-2 * MPa, 1e-8);
  expect_near("psat 500 K", IF97::psat97(500), 0.263889776e1 * MPa, 1e-8);
  expect_near("psat 600 K", IF97::psat97(600), 0.123443146e2 * MPa, 1e-8);
  expect_near("Tsat 0.1 MPa", IF97::Tsat97(0.1 * MPa), 0.372755919e3, 1e-8);
  expect_near("Tsat 1 MPa", IF97::Tsat97(1 * MPa), 0.453035632e3, 1e-8);
  expect_near("Tsat 10 MPa", IF97::Tsat97(10 * MPa), 0.584149488e3, 1e-8);

  // Two-phase mixing at 1 MPa.
  const double p = 1 * MPa;
  expect_near("hmass_pQ Q=0", IF97::hmass_pQ(p, 0), IF97::hliq_p(p), 1e-12);
  expect_near("hmass_pQ Q=1", IF97::hmass_pQ(p, 1), IF97::hvap_p(p), 1e-12);
  expect_near("Q_phmass", IF97::Q_phmass(p, IF97::hmass_pQ(p, 0.3)), 0.3, 1e-9);
  expect_near("Q_psmass", IF97::Q_psmass(p, IF97::smass_pQ(p, 0.7)), 0.7, 1e-9);
  expect(IF97::rholiq_p(p) > 100 * IF97::rhovap_p(p),
         "saturated liquid denser than vapor");
  expect(IF97::Q_phmass(30 * MPa, 500 * kJ) == 0,
         "compressed liquid above Pcrit has quality 0");

  // IAPWS R1-76(2014), its equation's surface tension rounded to 0.01 mN/m
  // (IF97.cpp lists 75.64 and 58.92).
  expect_near("sigma 0.01 C", IF97::sigma97(273.16), 75.65e-3, 1e-4);
  expect_near("sigma 100 C", IF97::sigma97(373.15), 58.91e-3, 1e-4);
}

void check_backward() {
  // IAPWS R7-97(2012) Tables 7 and 24, T(p,h); Tables 9 and 29, T(p,s).
  expect_near("T(p,h) 1", IF97::T_phmass(3 * MPa, 500 * kJ), 391.798509, 1e-8);
  expect_near("T(p,h) 1", IF97::T_phmass(80 * MPa, 1500 * kJ), 611.041229,
              1e-8);
  expect_near("T(p,h) 2a", IF97::T_phmass(0.001 * MPa, 3000 * kJ), 534.433241,
              1e-8);
  expect_near("T(p,h) 2a", IF97::T_phmass(3 * MPa, 4000 * kJ), 1010.77577,
              1e-8);
  expect_near("T(p,h) 2b", IF97::T_phmass(5 * MPa, 3500 * kJ), 801.299102,
              1e-8);
  expect_near("T(p,s) 1", IF97::T_psmass(3 * MPa, 0.5 * kJ), 307.842258, 1e-8);
  expect_near("T(p,s) 1", IF97::T_psmass(80 * MPa, 3 * kJ), 565.899909, 1e-8);
  expect_near("T(p,s) 2a", IF97::T_psmass(0.1 * MPa, 7.5 * kJ), 399.517097,
              1e-8);
  expect_near("T(p,s) 2a", IF97::T_psmass(2.5 * MPa, 8 * kJ), 1039.84917, 1e-8);

  // IAPWS SR2-01(2014) Tables 3 and 9 and SR4-04(2014) Tables 5 and 29.
  expect_near("p(h,s) 1", IF97::p_hsmass(1500 * kJ, 3.4 * kJ),
              58.68294423 * MPa, 1e-8);
  expect_near("p(h,s) 2a", IF97::p_hsmass(2800 * kJ, 6.5 * kJ),
              1.371012767 * MPa, 1e-8);
  expect_near("p(h,s) 2b", IF97::p_hsmass(2800 * kJ, 6 * kJ), 4.793911442 * MPa,
              1e-8);
  expect_near("p(h,s) 2c", IF97::p_hsmass(2800 * kJ, 5.1 * kJ),
              94.3920206 * MPa, 1e-8);
  expect_near("p(h,s) 3a", IF97::p_hsmass(1700 * kJ, 3.8 * kJ),
              25.55703246 * MPa, 1e-8);
  expect_near("p(h,s) 3b", IF97::p_hsmass(2600 * kJ, 5.1 * kJ),
              34.34999263 * MPa, 1e-8);
  expect_near("Tsat(h,s)", IF97::T_hsmass(1800 * kJ, 5.3 * kJ), 346.8475498,
              1e-8);
}

void check_transport() {
  // IAPWS R12-08 Table 4, viscosity at given density [uPa s].
  expect_near("visc 298.15 K", IF97::visc_TRho(298.15, 998), 889.7351e-6, 1e-6);
  expect_near("visc 873.15 K", IF97::visc_TRho(873.15, 600), 77.430195e-6,
              1e-6);
  // IAPWS R15-11 Tables 7 and 8, the industrial formulation over IF97
  // densities, thermal conductivity [W/m/K].
  expect_near("tcond 620 K", IF97::tcond_Tp(620, 20 * MPa), 0.481485195, 1e-8);
  expect_near("tcond 650 K", IF97::tcond_Tp(650, 0.3 * MPa), 0.0522311024,
              1e-8);
}

void check_errors() {
  throws<std::out_of_range>("T below Tmin",
                            [] { (void)IF97::rhomass_Tp(200, 0.1 * MPa); });
  throws<std::out_of_range>("p above Pmax",
                            [] { (void)IF97::rhomass_Tp(300, 200 * MPa); });
  throws<std::out_of_range>("region 5 above 50 MPa",
                            [] { (void)IF97::hmass_Tp(1500, 60 * MPa); });
  throws<std::out_of_range>("quality above Pcrit",
                            [] { (void)IF97::hmass_pQ(30 * MPa, 0.5); });
  throws<std::out_of_range>("quality above 1",
                            [] { (void)IF97::hmass_pQ(1 * MPa, 1.5); });
  throws<std::invalid_argument>("supercritical quality", [] {
    (void)IF97::Q_phmass(30 * MPa, 3000 * kJ);
  });
}

}  // namespace

int main() {
  if (IF97::get_if97_version() != "v2.2.1") {
    std::printf("FAIL version %s\n", IF97::get_if97_version().c_str());
    ++failures;
  }
  check_forward();
  check_saturation();
  check_backward();
  check_transport();
  check_errors();
  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("if97: ok\n");
  return 0;
}
