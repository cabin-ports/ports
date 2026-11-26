#include <polylineencoder.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

using Enc = gepaf::PolylineEncoder<>;
using Enc6 = gepaf::PolylineEncoder<6>;

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

struct Pos {
  double lat, lon;
};

} // namespace

int main() {
  // Google's documented example.
  const std::string example = "_p~iF~ps|U_ulLnnqC_mqNvxq`@";
  Enc e;
  e.addPoint(38.5, -120.2);
  e.addPoint(40.7, -120.95);
  e.addPoint(43.252, -126.453);
  check(e.encode() == example, "encode Google example");
  check(e.polyline().size() == 3, "polyline holds three points");
  check(Enc::encode(e.polyline()) == example, "static encode");

  const Enc::Polyline back = Enc::decode(example);
  check(back.size() == 3, "decode count");
  if (back.size() == 3) {
    check(near(back[0].latitude(), 38.5) && near(back[0].longitude(), -120.2),
          "decode point 0");
    check(near(back[1].latitude(), 40.7) && near(back[1].longitude(), -120.95),
          "decode point 1");
    check(near(back[2].latitude(), 43.252) &&
              near(back[2].longitude(), -126.453),
          "decode point 2");
  }

  // Empty polyline.
  Enc empty;
  check(empty.encode().empty(), "empty encodes to empty string");
  check(Enc::decode("").empty(), "empty decodes to no points");
  e.clear();
  check(e.polyline().empty() && e.encode().empty(), "clear");

  // Zero and negative coordinates.
  Enc z;
  z.addPoint(0.0, 0.0);
  check(z.encode() == "??", "zero point");
  Enc neg;
  neg.addPoint(-0.00001, 0.0);
  check(neg.encode() == "@?", "smallest negative latitude");
  Enc west;
  west.addPoint(0.0, -179.9832104);
  check(west.encode() == "?`~oia@", "negative longitude (Google spec value)");
  const Enc::Polyline w = Enc::decode("?`~oia@");
  check(w.size() == 1 && near(w[0].latitude(), 0.0) &&
            near(w[0].longitude(), -179.98321),
        "decode negative longitude, rounded to 5 digits");

  // Truncated input is rejected as a whole.
  check(Enc::decode("_p~iF~ps|U_ulLnnqC_mqNvxq`").empty(), "truncated input");

  // Precision template parameter.
  check(Enc::Precision::Value == 100000, "precision 5");
  check(Enc6::Precision::Value == 1000000, "precision 6");
  const Enc6::Polyline p6 = Enc6::decode("kkwayAmfxu^eAaH");
  check(p6.size() == 2, "precision 6 decode count");
  if (p6.size() == 2) {
    check(near(p6[0].latitude(), 47.231174) &&
              near(p6[0].longitude(), 16.626295),
          "precision 6 point 0");
    check(near(p6[1].latitude(), 47.231209) &&
              near(p6[1].longitude(), 16.62644),
          "precision 6 point 1");
  }
  Enc6 e6;
  e6.addPoint(47.231174, 16.626295);
  e6.addPoint(47.231209, 16.626440);
  check(e6.encode() == "kkwayAmfxu^eAaH", "precision 6 encode");

  // Generic range encode over a C array and a std::vector.
  const Pos arr[] = {{38.5, -120.2}, {40.7, -120.95}, {43.252, -126.453}};
  auto lat = [](const Pos &p) { return p.lat; };
  auto lon = [](const Pos &p) { return p.lon; };
  check(Enc::encode(arr, arr + 3, lat, lon) == example, "range over array");
  const std::vector<Pos> vec(arr, arr + 3);
  check(Enc::encode(vec.begin(), vec.end(), lat, lon) == example,
        "range over vector");

  if (failures == 0)
    std::puts("polylineencoder: all checks passed");
  return failures == 0 ? 0 : 1;
}
