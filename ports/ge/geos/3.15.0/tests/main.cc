#ifndef USE_UNSTABLE_GEOS_CPP_API
#error "geos must define USE_UNSTABLE_GEOS_CPP_API for its consumers"
#endif

#include <geos/algorithm/Orientation.h>
#include <geos/geom/Coordinate.h>
#include <geos/geom/Geometry.h>
#include <geos/geom/GeometryFactory.h>
#include <geos/io/ParseException.h>
#include <geos/io/WKTReader.h>
#include <geos/io/WKTWriter.h>
#include <geos/math/DD.h>
#include <geos/operation/valid/IsValidOp.h>
#include <geos/operation/valid/MakeValid.h>
#include <geos/version.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>

using geos::geom::CoordinateXY;
using geos::math::DD;

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

// Upstream's DDTest checkBinomialSquare: exact in double-double arithmetic.
static bool binomial_square_exact(double a, double b) {
  DD add(a);
  DD bdd(b);
  DD sum_ab = add + bdd;
  DD square = sum_ab * sum_ab;
  DD ab = add * bdd;
  DD expansion = bdd * bdd + ab + ab;
  DD diff = square - add * add;
  return diff == expansion && (diff - expansion).isZero();
}

int main() {
  check(std::strcmp(GEOS_VERSION, "3.15.0") == 0, "GEOS_VERSION");
  check(GEOS_VERSION_MAJOR == 3 && GEOS_VERSION_MINOR == 15, "version macros");

  // DDTest testDeterminantRobust and testBinom.
  check(DD::determinant(1.0e9, 1.0e9 - 1, 1.0e9 - 1, 1.0e9 - 2) == DD(-1.0),
        "DD determinant");
  check(binomial_square_exact(1e14, 291.0), "DD binomial 1e14, 291");
  check(binomial_square_exact(5e14, 345291.0), "DD binomial 5e14, 345291");
  // DDTest testReciprocal.  On arm64, 314159269 fails if fused
  // multiply-adds slip in, as they do without patch 0002.
  for (double x : {3.0, 999.0, 314159269.0}) {
    DD r(x);
    check((r - r.reciprocal().reciprocal()).doubleValue() <= 0.0,
          "DD reciprocal");
  }

  // OrientationIndexFailureTest case 1, as in main.c.
  CoordinateXY p1(1.4540766091864998, -7.989685402102996);
  CoordinateXY p2(23.131039116367354, -7.004368924503866);
  CoordinateXY p3(1.4540766091865, -7.989685402102996);
  using geos::algorithm::Orientation;
  int o = Orientation::index(p1, p2, p3);
  check(o != 0 && o == Orientation::index(p2, p3, p1) &&
            o == Orientation::index(p3, p1, p2) &&
            o == -Orientation::index(p1, p3, p2),
        "robust orientation");

  auto factory = geos::geom::GeometryFactory::create();
  geos::io::WKTReader reader(*factory);
  auto shell = reader.read(
      "POLYGON ((0 0, 20 0, 20 20, 0 20, 0 0), (5 5, 5 15, 15 15, 15 5, 5 5))");
  auto line = reader.read("LINESTRING (-5 10, 25 10)");
  check(shell->getArea() == 300.0, "polygon with hole area");
  check(shell->isValid(), "polygon validity");
  bool threw = false;
  try {
    reader.read("POLYGON ((0 0, 1");
  } catch (const geos::io::ParseException &) {
    threw = true;
  }
  check(threw, "malformed WKT throws ParseException");

  auto clipped = shell->intersection(line.get());
  check(clipped->getLength() == 10.0, "line clipped by polygon with hole");
  check(clipped->getNumGeometries() == 2, "clipped line has two parts");

  auto bowtie = reader.read("POLYGON ((0 0, 10 10, 10 0, 0 10, 0 0))");
  geos::operation::valid::IsValidOp validity(bowtie.get());
  check(!validity.isValid(), "self-intersecting polygon is invalid");
  auto fixed = geos::operation::valid::MakeValid().build(bowtie.get());
  check(fixed->isValid() && fixed->getArea() == 50.0 &&
            fixed->getNumGeometries() == 2,
        "MakeValid splits the bowtie into two triangles");

  geos::io::WKTWriter writer;
  writer.setTrim(true);
  clipped->normalize();
  check(writer.write(clipped.get()) ==
            "MULTILINESTRING ((15 10, 20 10), (0 10, 5 10))",
        "normalized clipped line WKT");

  if (failures != 0) {
    return 1;
  }
  std::printf("GEOS %s: polygon with a hole has area %g\n", GEOS_VERSION,
              shell->getArea());
  return 0;
}
