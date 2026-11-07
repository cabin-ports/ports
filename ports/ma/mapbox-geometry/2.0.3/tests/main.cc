// Maps a park with a pond, a road and a station as mapbox::geometry
// features: their envelope and point count through the geometry variant,
// a translation through the mutable for_each_point, point arithmetic,
// property values, and the JSON-like stream output (of geometries other
// than empty only off clang, where streaming a point is ambiguous).
// C++14, the floor of geometry.hpp.
#include <mapbox/feature.hpp>
#include <mapbox/geometry.hpp>
#include <mapbox/geometry_io.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

namespace {

namespace geo = mapbox::geometry;
namespace feat = mapbox::feature;

using point = geo::point<double>;

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

template <typename T>
std::string str(const T& value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

const char* kind(const geo::geometry<double>& g) {
  return g.match([](const geo::polygon<double>&) { return "polygon"; },
                 [](const geo::line_string<double>&) { return "line_string"; },
                 [](const point&) { return "point"; }, [](const auto&) { return "other"; });
}

std::size_t count_points(const geo::geometry<double>& g) {
  std::size_t n = 0;
  geo::for_each_point(g, [&n](const point&) { ++n; });
  return n;
}

}  // namespace

int main() {
  const geo::polygon<double> park{
      {{0, 0}, {8, 0}, {8, 6}, {0, 6}, {0, 0}},
      {{2, 2}, {4, 2}, {4, 4}, {2, 4}, {2, 2}},
  };
  const geo::line_string<double> road{{-2, 3}, {10, 3}};

  feat::feature_collection<double> layer{
      feat::feature<double>{geo::geometry<double>{park},
                            feat::property_map{{"name", "Central"}, {"area", 3.5}, {"ponds", 1}},
                            feat::identifier{std::string("park")}},
      feat::feature<double>{geo::geometry<double>{road},
                            feat::property_map{{"lanes", 2u}, {"oneway", false}},
                            feat::identifier{std::uint64_t(7)}},
      feat::feature<double>{geo::geometry<double>{point(5, -1)}},
  };

  check(layer.size() == 3 && std::string(kind(layer[0].geometry)) == "polygon" &&
            std::string(kind(layer[1].geometry)) == "line_string" &&
            std::string(kind(layer[2].geometry)) == "point" &&
            layer[2].geometry.get<point>() == point(5, -1),
        "the geometry variant holds each kind");
  check(count_points(layer[0].geometry) == 10 && count_points(layer[1].geometry) == 2 &&
            count_points(layer[2].geometry) == 1,
        "for_each_point visits the hole's ring too");

  // The whole layer as one geometry collection.
  geo::geometry_collection<double> all;
  for (const auto& f : layer) all.push_back(f.geometry);
  geo::geometry<double> scene{all};
  check(count_points(scene) == 13, "for_each_point descends into the collection");
  check(geo::envelope(scene) == geo::box<double>({-2, -1}, {10, 6}),
        "envelope spans the road and the station");

  // Shift everything 10 east and halve it about the origin.
  geo::for_each_point(scene, [](point& p) {
    p += point(10, 0);
    p = p / 2.0;
  });
  check(geo::envelope(scene) == geo::box<double>({4, -0.5}, {10, 3}),
        "for_each_point moves every point in place");
  check(scene.get<geo::geometry_collection<double>>()[2].get<point>() == point(7.5, -0.5),
        "the station moved with it");
  check(layer[2].geometry.get<point>() == point(5, -1), "the layer's own geometry is a separate copy");

  // Integer coordinates take the envelope's numeric_limits::max/min path.
  check(geo::envelope(geo::line_string<std::int64_t>{{-3, 7}, {4, -2}}) ==
            geo::box<std::int64_t>({-3, -2}, {4, 7}),
        "envelope of int64_t points");
  check(point(1, 2) + point(3, 4) == point(4, 6) && point(1, 2) * 3.0 == point(3, 6) &&
            point(5, 5) - 1.0 == point(4, 4) && point(1, 2) != point(2, 1),
        "point arithmetic");

  // Property values pick int64_t for signed, uint64_t for unsigned.
  const feat::property_map& props = layer[0].properties;
  check(props.at("ponds").getInt() != nullptr && *props.at("ponds").getInt() == 1 &&
            props.at("ponds").getUint() == nullptr && *props.at("area").getDouble() == 3.5 &&
            *props.at("name").getString() == "Central",
        "park properties");
  const feat::property_map& road_props = layer[1].properties;
  check(road_props.at("lanes").getUint() != nullptr && *road_props.at("lanes").getUint() == 2 &&
            road_props.at("oneway").getBool() != nullptr && !*road_props.at("oneway").getBool(),
        "road properties");
  check(layer[1].id.is<std::uint64_t>() && layer[1].id.get<std::uint64_t>() == 7 &&
            layer[2].id.is<feat::null_value_t>() && layer[2].properties.empty(),
        "identifiers");

  // Arrays compare by their elements, not by their shared pointers.
  const feat::value tags(feat::value::array_type{"trees", 3});
  check(tags == feat::value(feat::value::array_type{"trees", 3}) &&
            !(tags == feat::value(feat::value::array_type{"trees", 4})) &&
            tags.getArray()->size() == 2,
        "array values");

  feat::feature<double> copy = layer[0];
  check(copy == layer[0], "a copied feature is equal");
  copy.properties["ponds"] = 2;
  check(copy != layer[0], "a changed property makes it differ");

  check(str(geo::empty()) == "[]", "empty output");
#ifndef __clang__
  check(str(layer[2].geometry) == "[5,-1]" && str(road) == "[[-2,3],[10,3]]" &&
            str(park) == "[[[0,0],[8,0],[8,6],[0,6],[0,0]],[[2,2],[4,2],[4,4],[2,4],[2,2]]]",
        "geometry output");
#endif
  check(str(props) == "{\"area\":3.5,\"name\":\"Central\",\"ponds\":1}" &&
            str(road_props) == "{\"lanes\":2,\"oneway\":false}" && str(tags) == "[\"trees\",3]" &&
            str(layer[0].id) == "\"park\"" && str(layer[1].id) == "7" && str(layer[2].id) == "null",
        "property and identifier output");

  std::printf("mapbox-geometry (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
