// Expected values are worked out by hand from the unit definitions: 1 in = 2.54 cm,
// 1 ft = 12 in, 1 mi = 5280 ft, 0 degC = 273.15 K, 0 K = -459.67 degF, c = 299792458 m/s.
#include "au/au.hh"
#include "au/constants/speed_of_light.hh"
#include "au/io.hh"
#include "au/units/celsius.hh"
#include "au/units/degrees.hh"
#include "au/units/fahrenheit.hh"
#include "au/units/feet.hh"
#include "au/units/hours.hh"
#include "au/units/inches.hh"
#include "au/units/kelvins.hh"
#include "au/units/meters.hh"
#include "au/units/miles.hh"
#include "au/units/minutes.hh"
#include "au/units/seconds.hh"
#include "au/units/unos.hh"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>

namespace {

using namespace au;

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

bool near(double actual, double expected, double tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

template <typename T>
std::string str(const T &x) {
    std::ostringstream out;
    out << x;
    return out.str();
}

// Integer conversions with an integer factor are exact and usable at compile time.
static_assert(feet(3) == inches(36), "");
static_assert(miles(1).as(feet) == feet(5280), "");
static_assert(hours(2).in(seconds) == 7200, "");
static_assert(std::is_same<decltype(feet(3).in(inches)), int>::value, "");

void testConversions() {
    CHECK(meters(1.5).in(centi(meters)) == 150.0);
    CHECK(kilo(meters)(1.5).in(meters) == 1500.0);
    CHECK(near(inches(1.0).in(centi(meters)), 2.54, 1e-15));
    CHECK(near(miles(1.0).in(meters), 1609.344, 1e-12));
    CHECK(near(meters(1.0).in(inches), 1.0/0.0254, 1e-12));

    // Dividing an int by 12 can truncate, so it needs an explicit policy; it truncates toward zero.
    CHECK(inches(24).as(feet, ignore(TRUNCATION_RISK)) == feet(2));
    CHECK(inches(23).as(feet, ignore(TRUNCATION_RISK)) == feet(1));
    CHECK(inches(-23).as(feet, ignore(TRUNCATION_RISK)) == feet(-1));
    CHECK(inches(27.8).as<int>(feet, ignore(TRUNCATION_RISK)) == feet(2));
    CHECK(minutes(150).in(hours, ignore(TRUNCATION_RISK)) == 2);

    CHECK(is_conversion_lossy(inches(23), feet));
    CHECK(!is_conversion_lossy(inches(24), feet));
}

void testRounding() {
    // round_* follow std::round: the result is floating point and halves round away from zero.
    CHECK(round_as(meters, centi(meters)(149)) == meters(1.0));
    static_assert(std::is_same<decltype(round_in(meters, centi(meters)(149))), double>::value, "");
    CHECK(round_in(meters, centi(meters)(150)) == 2.0);
    CHECK(round_in(meters, centi(meters)(-150)) == -2.0);
    CHECK(round_in(feet, inches(17.9)) == 1.0);
    CHECK(floor_in(feet, inches(-1)) == -1.0);
    CHECK(ceil_in(feet, inches(1)) == 1.0);
    // int_round_* stay integral.
    static_assert(std::is_same<decltype(int_round_in(meters, centi(meters)(250))), int>::value, "");
    CHECK(int_round_as(meters, centi(meters)(249)) == meters(2));
    CHECK(int_round_in(meters, centi(meters)(250)) == 3);
    CHECK(int_round_in(meters, centi(meters)(-250)) == -3);
}

void testCompound() {
    CHECK((meters(10.0) / seconds(2.0)).in(meters / second) == 5.0);
    CHECK((kilo(meters) / hour)(36).in(meters / second, ignore(TRUNCATION_RISK)) == 10);
    CHECK(near((kilo(meters) / hour)(36.0).in(meters / second), 10.0, 1e-12));
    CHECK((meters(3) * meters(4)).in(squared(meters)) == 12);
    CHECK(int_pow<2>(feet(3)) == squared(feet)(9));
    CHECK(sqrt(squared(meters)(9.0)) == meters(3.0));
    CHECK((feet(6.0) / inches(3.0)).in(unos) == 24.0);

    CHECK(SPEED_OF_LIGHT.as<long long>(meters / second) == (meters / second)(299792458LL));
    CHECK((SPEED_OF_LIGHT * seconds(2LL)).in(meters) == 599584916LL);
    CHECK(near((SPEED_OF_LIGHT * milli(seconds)(1.0)).in(kilo(meters)), 299.792458, 1e-12));
}

void testTemperature() {
    CHECK(near(celsius_pt(100.0).in(kelvins_pt), 373.15, 1e-12));
    CHECK(near(fahrenheit_pt(212.0).in(celsius_pt), 100.0, 1e-12));
    CHECK(near(fahrenheit_pt(32.0).in(celsius_pt), 0.0, 1e-12));
    CHECK(near(kelvins_pt(0.0).in(fahrenheit_pt), -459.67, 1e-12));
    // 20 degC = 293.15 K exactly, in integer centikelvins.
    CHECK(celsius_pt(20).as(centi(kelvins_pt)) == centi(kelvins_pt)(29315));
    // Point differences are quantities; a celsius step equals a kelvin step.
    CHECK(celsius_pt(25) - celsius_pt(20) == celsius_qty(5));
    CHECK(celsius_qty(5) == kelvins(5));
}

void testMath() {
    CHECK(abs(meters(-2)) == meters(2));
    CHECK(max(meters(1), centi(meters)(150)) == centi(meters)(150));
    CHECK(min(feet(1), inches(13)) == inches(12));
    CHECK(hypot(meters(3.0), meters(4.0)) == meters(5.0));
    CHECK(near(sin(degrees(30.0)), 0.5, 1e-15));
    CHECK(near(cos(degrees(60.0)), 0.5, 1e-15));
    CHECK(meters(5) > ZERO && meters(-5) < ZERO);
}

void testChrono() {
    CHECK(as_chrono_duration(seconds(2)) == std::chrono::seconds(2));
    const auto ms = as_chrono_duration(milli(seconds)(1500));
    static_assert(std::is_same<std::decay_t<decltype(ms)>, std::chrono::duration<int, std::milli>>::value, "");
    CHECK(ms.count() == 1500);
    // A chrono duration converts implicitly to the corresponding quantity.
    const Quantity<Milli<Seconds>, std::chrono::milliseconds::rep> quarter = std::chrono::milliseconds(250);
    CHECK(quarter.in(milli(seconds)) == 250);
    CHECK(quarter == milli(seconds)(250));
    CHECK(as_quantity(std::chrono::minutes(3)) == seconds(180));
}

void testIo() {
    CHECK(str(meters(2)) == "2 m");
    CHECK(str(centi(meters)(150)) == "150 cm");
    CHECK(str((meters / second)(5)) == "5 m / s");
    CHECK(str((kilo(meters) / hour)(36)) == "36 km / h");
    CHECK(str(squared(meters)(12)) == "12 m^2");
    CHECK(str(celsius_pt(20)) == "@(20 degC)");
    CHECK(str(SPEED_OF_LIGHT) == "c");
    CHECK(str(ZERO) == "0");
    // A char-sized rep prints as a number, not a character.
    CHECK(str(meters(static_cast<std::int8_t>(65))) == "65 m");
}

}  // namespace

int main() {
    testConversions();
    testRounding();
    testCompound();
    testTemperature();
    testMath();
    testChrono();
    testIo();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("au: ok\n");
    return 0;
}
