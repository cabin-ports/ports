// Round-trips one object graph through all four archives: strings,
// containers, shared pointers that must stay shared, a unique pointer,
// polymorphic types behind base pointers, and a versioned class.  Also
// checks exact JSON and portable-binary bytes and the errors for missing
// fields, malformed JSON and truncated binary input.  Kept C++11-clean.
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <cereal/archives/xml.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/version.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

struct Point {
  int x, y;
  Point() : x(0), y(0) {}
  Point(int x_, int y_) : x(x_), y(y_) {}
  bool operator==(const Point& o) const { return x == o.x && y == o.y; }
  template <class Archive>
  void serialize(Archive& ar) {
    ar(CEREAL_NVP(x), CEREAL_NVP(y));
  }
};

struct Shape {
  virtual ~Shape() {}
  virtual double area() const = 0;
  template <class Archive>
  void serialize(Archive&) {}
};

struct Rect : Shape {
  double w, h;
  Rect() : w(0), h(0) {}
  Rect(double w_, double h_) : w(w_), h(h_) {}
  double area() const override { return w * h; }
  template <class Archive>
  void serialize(Archive& ar) {
    ar(cereal::base_class<Shape>(this), CEREAL_NVP(w), CEREAL_NVP(h));
  }
};

struct Square : Shape {
  double side;
  Square() : side(0) {}
  explicit Square(double s) : side(s) {}
  double area() const override { return side * side; }
  template <class Archive>
  void serialize(Archive& ar) {
    ar(cereal::base_class<Shape>(this), CEREAL_NVP(side));
  }
};

CEREAL_REGISTER_TYPE(Rect)
CEREAL_REGISTER_TYPE(Square)

// Split save/load with a class version; load records what it read.
struct Settings {
  std::string name;
  std::uint32_t loaded_version;
  Settings() : loaded_version(0) {}
  template <class Archive>
  void save(Archive& ar, std::uint32_t const) const {
    ar(CEREAL_NVP(name));
  }
  template <class Archive>
  void load(Archive& ar, std::uint32_t const version) {
    ar(CEREAL_NVP(name));
    loaded_version = version;
  }
};
CEREAL_CLASS_VERSION(Settings, 2)

struct Inventory {
  std::string owner;
  std::vector<Point> points;
  std::map<std::string, int> counts;
  std::shared_ptr<Point> a, b;
  std::unique_ptr<Point> solo;
  std::vector<std::shared_ptr<Shape>> shapes;
  Settings settings;
  template <class Archive>
  void serialize(Archive& ar) {
    ar(CEREAL_NVP(owner), CEREAL_NVP(points), CEREAL_NVP(counts),
       CEREAL_NVP(a), CEREAL_NVP(b), CEREAL_NVP(solo), CEREAL_NVP(shapes),
       CEREAL_NVP(settings));
  }
};

static Inventory sample() {
  Inventory inv;
  inv.owner = "caf\xc3\xa9 \"quoted\" <xml> & json";
  inv.points.push_back(Point(1, 2));
  inv.points.push_back(Point(-3, 40000));
  inv.counts["apples"] = 3;
  inv.counts["pears"] = -7;
  inv.a = std::make_shared<Point>(7, 8);
  inv.b = inv.a;
  inv.solo.reset(new Point(9, 10));
  std::shared_ptr<Shape> rect = std::make_shared<Rect>(2.5, 4);
  inv.shapes.push_back(rect);
  inv.shapes.push_back(std::make_shared<Square>(3));
  inv.shapes.push_back(rect);
  inv.settings.name = "fast";
  return inv;
}

static void check_same(const Inventory& in, const Inventory& out) {
  CHECK(out.owner == in.owner);
  CHECK(out.points == in.points);
  CHECK(out.counts == in.counts);
  CHECK(out.a && out.a == out.b && *out.a == Point(7, 8));
  CHECK(out.solo && *out.solo == Point(9, 10));
  CHECK(out.shapes.size() == 3);
  CHECK(dynamic_cast<Rect*>(out.shapes[0].get()) != nullptr);
  CHECK(out.shapes[0]->area() == 10.0);
  CHECK(dynamic_cast<Square*>(out.shapes[1].get()) != nullptr);
  CHECK(out.shapes[1]->area() == 9.0);
  CHECK(out.shapes[2] == out.shapes[0]);
  CHECK(out.settings.name == "fast" && out.settings.loaded_version == 2);
}

template <class Out, class In>
static std::string round_trip() {
  const Inventory in = sample();
  std::stringstream ss;
  {
    Out ar(ss);
    ar(cereal::make_nvp("inventory", in));
  }
  const std::string bytes = ss.str();
  Inventory out;
  {
    In ar(ss);
    ar(cereal::make_nvp("inventory", out));
  }
  check_same(in, out);
  return bytes;
}

template <class Exception, class In>
static std::string load_error(const std::string& data) {
  std::istringstream is(data);
  try {
    In ar(is);
    Inventory out;
    ar(cereal::make_nvp("inventory", out));
  } catch (const Exception& e) {
    return e.what();
  }
  return std::string();
}

int main() {
  CHECK(CEREAL_VERSION_MAJOR == 1 && CEREAL_VERSION_MINOR == 3 &&
        CEREAL_VERSION_PATCH == 2);

  const std::string binary =
      round_trip<cereal::BinaryOutputArchive, cereal::BinaryInputArchive>();
  round_trip<cereal::PortableBinaryOutputArchive,
             cereal::PortableBinaryInputArchive>();
  const std::string json =
      round_trip<cereal::JSONOutputArchive, cereal::JSONInputArchive>();
  const std::string xml =
      round_trip<cereal::XMLOutputArchive, cereal::XMLInputArchive>();
  CHECK(json.find("\"cereal_class_version\": 2") != std::string::npos);
  CHECK(json.find("\"polymorphic_name\": \"Square\"") != std::string::npos);
  CHECK(xml.find("<owner>caf\xc3\xa9 &quot;quoted&quot; &lt;xml&gt; &amp; "
                 "json</owner>") != std::string::npos);

  // Exact JSON for a small struct; NoIndent keeps PrettyWriter's line
  // breaks but drops the indentation.
  {
    std::ostringstream os;
    {
      cereal::JSONOutputArchive ar(os,
                                   cereal::JSONOutputArchive::Options::NoIndent());
      ar(cereal::make_nvp("p", Point(1, -2)));
    }
    CHECK(os.str() == "{\n\"p\": {\n\"x\": 1,\n\"y\": -2\n}\n}");
  }

  // Portable binary: an endianness flag, then the value in the byte order
  // the writer asked for, whatever the host's.  The reader's options name
  // the order to load into, here the host's.
  {
    std::ostringstream os;
    {
      cereal::PortableBinaryOutputArchive ar(
          os, cereal::PortableBinaryOutputArchive::Options::BigEndian());
      ar(std::uint32_t(0x01020304));
    }
    CHECK(os.str() == std::string("\x00\x01\x02\x03\x04", 5));
    std::istringstream is(os.str());
    cereal::PortableBinaryInputArchive in(is);
    std::uint32_t value = 0;
    in(value);
    CHECK(value == 0x01020304u);
  }

  // Errors.
  {
    std::istringstream is("{\"p\": {\"x\": 1}}");
    cereal::JSONInputArchive ar(is);
    Point p;
    bool threw = false;
    try {
      ar(cereal::make_nvp("p", p));
    } catch (const cereal::Exception& e) {
      threw = std::string(e.what()).find("NVP (y) not found") !=
              std::string::npos;
    }
    CHECK(threw);
  }
  // cereal ignores RapidJSON's parse error; reading the resulting Null
  // document as an object throws.
  const std::string malformed =
      load_error<cereal::RapidJSONException, cereal::JSONInputArchive>(
          "{\"inventory\": ");
  CHECK(!malformed.empty());
  const std::string truncated =
      load_error<cereal::Exception, cereal::BinaryInputArchive>(
          binary.substr(0, binary.size() / 2));
  CHECK(truncated.find("Failed to read") != std::string::npos);

  std::puts("ok");
  return 0;
}
