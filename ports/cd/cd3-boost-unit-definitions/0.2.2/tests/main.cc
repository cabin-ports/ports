// Quantities in the header's own units, converted through Boost.Units to exact values.
#include <BoostUnitDefinitions/Units.hpp>
#include <cmath>
#include <sstream>

using namespace boost::units;

static bool near(double a, double b) { return std::fabs(a - b) <= 1e-9 * std::fabs(b); }

int main() {
    // Imperial and prefixed metric lengths convert to SI meters.
    quantity<si::length> ft(3.0 * i::ft);
    if (!near(ft.value(), 0.9144)) return 1;
    quantity<si::length> in(12.0 * i::inch);
    if (!near(in.value(), 0.3048)) return 2;
    quantity<si::length> yd(2.0 * i::yd);
    if (!near(yd.value(), 1.8288)) return 3;
    quantity<t::meter> um(2500.0 * i::um);
    if (!near(um.value(), 2.5e-3)) return 4;
    quantity<t::millimeter> km(0.001 * i::km);
    if (!near(km.value(), 1e3)) return 5;

    // Angles: 180 degrees is pi radians; 60 arcminutes is one degree.
    quantity<t::radian> half_turn(180.0 * i::deg);
    if (!near(half_turn.value(), 3.14159265358979323846)) return 6;
    quantity<t::degree> deg(60.0 * i::arcmin);
    if (!near(deg.value(), 1.0)) return 7;
    quantity<t::percent> p(0.25 * i::dimensionless);
    if (!near(p.value(), 25.0)) return 8;

    // Derived units: radiant exposure and irradiance across area scales.
    quantity<t::J_cm_n2> exposure = 1.0 * i::J / (1.0 * i::cm * i::cm);
    quantity<t::J_m_n2> per_m2(exposure);
    if (!near(per_m2.value(), 1e4)) return 9;
    quantity<t::mW_cm_n2> irradiance = 5.0 * i::mW / (1.0 * i::cm_p2);
    quantity<t::W_m_n2> w_m2(irradiance);
    if (!near(w_m2.value(), 50.0)) return 10;

    // Implicit conversion between equivalent types, and int operators the header adds.
    quantity<si::length> L = 3.0 * i::m;
    quantity<t::m> L2 = L;
    if (!near((2 * L2).value(), 6.0) || !near((L2 / 3).value(), 1.0)) return 11;
    quantity<si::frequency> f = 1 / (4.0 * i::s);
    if (!near(f.value(), 0.25)) return 12;

    // The header's IO overload names the unit.
    std::ostringstream os;
    os << symbol_format << exposure;
    if (os.str() != "1 J cm^-2") return 13;
    return 0;
}
