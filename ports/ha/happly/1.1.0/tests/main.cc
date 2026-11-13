// A C++11 consumer of happly (a C++-only library): builds a mesh with extra
// scalar and list properties, writes it as ASCII, little-endian and
// big-endian binary PLY files in a temporary directory it then removes,
// reads each file back, and checks the header text, the binary layout and
// byte order, exact values, and type promotion; then reads files in forms
// other writers produce, and checks error reporting.
#include "happly.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace {

int failures = 0;
std::string context = "setup";

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL [%s] %s:%d: %s\n", context.c_str(), __FILE__,     \
                  __LINE__, #cond);                                       \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

// True when f throws a std::runtime_error whose message contains fragment.
template <class F>
bool throws_with(F f, const char* fragment) {
  try {
    f();
  } catch (const std::runtime_error& e) {
    return std::strstr(e.what(), fragment) != NULL;
  }
  return false;
}

// Creates a fresh, empty directory under the system temporary directory.
bool make_temp_dir(std::string& path) {
#ifdef _WIN32
  char dir[MAX_PATH + 1];
  char name[MAX_PATH];
  // GetTempFileNameA reserves a unique name by creating a file there.
  if (GetTempPathA(MAX_PATH + 1, dir) == 0 ||
      GetTempFileNameA(dir, "ply", 0, name) == 0 || !DeleteFileA(name) ||
      !CreateDirectoryA(name, NULL)) {
    return false;
  }
  path = name;
  return true;
#else
  const char* dir = std::getenv("TMPDIR");
  path = std::string(dir && *dir ? dir : "/tmp") + "/happly-test-XXXXXX";
  return mkdtemp(&path[0]) != NULL;
#endif
}

// Fails unless the directory is empty.
bool remove_dir(const std::string& path) {
#ifdef _WIN32
  return RemoveDirectoryA(path.c_str()) != 0;
#else
  return rmdir(path.c_str()) == 0;
#endif
}

std::string read_file(const std::string& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

struct Mesh {
  std::vector<std::array<double, 3>> positions;
  std::vector<std::array<unsigned char, 3>> colors;
  std::vector<float> confidence;
  std::vector<int32_t> labels;
  std::vector<std::vector<int>> faces;
  std::vector<std::vector<uint16_t>> polylines;
};

Mesh make_mesh() {
  Mesh m;
  m.positions = {{{0.1, -2.5, 1.0 / 3.0}},
                 {{1e-7, 4.0, -1.25}},
                 {{-3.75, 2.0 / 3.0, 1e10}},
                 {{0.0, -0.5, 6.02214076e23}}};
  m.colors = {{{255, 0, 128}}, {{1, 254, 127}}, {{0, 0, 0}}, {{17, 34, 51}}};
  // 1000000256 needs all nine significant digits (max_digits10) to survive
  // ASCII, and 1/3 needs more than six.
  m.confidence = {1000000256.0f, 0.1f, -1.75f, 1.0f / 3.0f};
  m.labels = {-7, 0x12345678, std::numeric_limits<int32_t>::min(), 42};
  m.faces = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
  // Lists of different lengths, including an empty one.
  m.polylines = {{0, 1, 2, 3}, {}, {65535, 7}};
  return m;
}

void fill(happly::PLYData& ply, Mesh m) {
  ply.addVertexPositions(m.positions);
  ply.addVertexColors(m.colors);
  happly::Element& vertex = ply.getElement("vertex");
  vertex.addProperty<float>("confidence", m.confidence);
  vertex.addProperty<int32_t>("label", m.labels);
  ply.addFaceIndices(m.faces);
  ply.addElement("polyline", m.polylines.size());
  ply.getElement("polyline").addListProperty<uint16_t>("points", m.polylines);
  ply.comments.push_back("cabin-ports consumer test");
  ply.objInfoComments.push_back("tetrahedron");
}

// Everything after the "format" line that fill() and happly produce.
const char kHeaderTail[] =
    "comment cabin-ports consumer test\n"
    "comment Written with hapPLY (https://github.com/nmwsharp/happly)\n"
    "obj_info tetrahedron\n"
    "element vertex 4\n"
    "property double x\n"
    "property double y\n"
    "property double z\n"
    "property uchar red\n"
    "property uchar green\n"
    "property uchar blue\n"
    "property float confidence\n"
    "property int label\n"
    "element face 4\n"
    "property list uchar int vertex_indices\n"
    "element polyline 3\n"
    "property list uchar ushort points\n"
    "end_header\n";

// Vertices: 4 * (3 doubles + 3 uchars + float + int); faces: 4 * (uchar
// count + 3 ints); polylines: uchar counts plus 4, 0 and 2 ushorts.
const std::size_t kBinaryPayloadSize = 4 * (24 + 3 + 4 + 4) + 4 * (1 + 12) +
                                       (1 + 8) + 1 + (1 + 4);

void check_mesh(happly::PLYData& ply, const Mesh& m) {
  CHECK((ply.getElementNames() ==
         std::vector<std::string>{"vertex", "face", "polyline"}));
  CHECK(ply.getVertexPositions() == m.positions);
  CHECK(ply.getVertexColors() == m.colors);

  happly::Element& vertex = ply.getElement("vertex");
  CHECK(vertex.count == 4);
  CHECK((vertex.getPropertyNames() ==
         std::vector<std::string>{"x", "y", "z", "red", "green", "blue",
                                  "confidence", "label"}));
  CHECK(vertex.getProperty<float>("confidence") == m.confidence);
  CHECK(vertex.getProperty<int32_t>("label") == m.labels);
  // Reads promote to wider types of the same signedness, never narrow.
  const std::vector<double> widened = vertex.getProperty<double>("confidence");
  CHECK(widened.size() == 4 && widened[1] == static_cast<double>(0.1f));
  CHECK((vertex.getProperty<uint32_t>("red") ==
         std::vector<uint32_t>{255, 1, 0, 17}));
  CHECK(throws_with([&vertex] { vertex.getProperty<int32_t>("red"); },
                    "cannot be coerced"));
  CHECK(throws_with([&vertex] { vertex.getProperty<float>("x"); },
                    "cannot be coerced"));
  CHECK(throws_with([&vertex] { vertex.getProperty<int>("missing"); },
                    "does not have property"));

  CHECK(ply.getFaceIndices<int>() == m.faces);
  std::vector<std::vector<std::size_t>> wide_faces;
  for (const std::vector<int>& f : m.faces) {
    wide_faces.emplace_back(f.begin(), f.end());
  }
  CHECK(ply.getFaceIndices() == wide_faces);
  CHECK(ply.getElement("polyline").getListProperty<uint16_t>("points") ==
        m.polylines);

  CHECK((ply.comments ==
         std::vector<std::string>{
             "cabin-ports consumer test",
             "Written with hapPLY (https://github.com/nmwsharp/happly)"}));
  CHECK(ply.objInfoComments == std::vector<std::string>(1, "tetrahedron"));
}

// The bytes of v in little-endian or big-endian order, whatever the host.
std::string double_bytes(double v, bool big_endian) {
  uint64_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  std::string out(8, '\0');
  for (int i = 0; i < 8; ++i) {
    const int shift = 8 * (big_endian ? 7 - i : i);
    out[i] = static_cast<char>((bits >> shift) & 0xFF);
  }
  return out;
}

struct Format {
  happly::DataFormat format;
  const char* file;
  const char* name;
};

void round_trip_files(const std::string& dir, const Mesh& m) {
  const Format formats[] = {
      {happly::DataFormat::ASCII, "ascii.ply", "ascii"},
      {happly::DataFormat::Binary, "little.ply", "binary_little_endian"},
      {happly::DataFormat::BinaryBigEndian, "big.ply", "binary_big_endian"},
  };
  for (const Format& f : formats) {
    context = f.name;
    const std::string path = dir + "/" + f.file;
    try {
      happly::PLYData out;
      fill(out, m);
      out.write(path, f.format);

      const std::string contents = read_file(path);
      const std::string header =
          std::string("ply\nformat ") + f.name + " 1.0\n" + kHeaderTail;
      CHECK(contents.compare(0, header.size(), header) == 0);
      if (f.format != happly::DataFormat::ASCII) {
        const std::string payload =
            contents.size() > header.size() ? contents.substr(header.size()) : "";
        CHECK(payload.size() == kBinaryPayloadSize);
        // The first value is vertex 0's x coordinate.
        const bool big = f.format == happly::DataFormat::BinaryBigEndian;
        CHECK(payload.compare(0, 8, double_bytes(m.positions[0][0], big)) == 0);
      }

      happly::PLYData in(path);
      check_mesh(in, m);
    } catch (const std::exception& e) {
      std::printf("FAIL [%s] unexpected exception: %s\n", context.c_str(),
                  e.what());
      ++failures;
    }
    CHECK(std::remove(path.c_str()) == 0);
  }
}

void round_trip_stream(const Mesh& m) {
  context = "stream";
  happly::PLYData out;
  fill(out, m);
  std::stringstream stream;
  out.write(stream, happly::DataFormat::BinaryBigEndian);
  happly::PLYData in(stream);
  check_mesh(in, m);
}

// Files in forms happly never writes: type-name aliases, int and ushort list
// counts, a blank line before the data, CRLF line endings, and big-endian
// data written elsewhere.
void read_foreign() {
  context = "foreign ascii";
  {
    std::istringstream in(
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 2\n"
        "property float32 x\n"
        "property int16 t\n"
        "property uint8 id\n"
        "element face 1\n"
        "property list int uint vertex_indices\n"
        "end_header\n"
        "\n"
        "1.5 -3 200\n"
        "-0.25 7 9\n"
        "3 0 1 1\n");
    happly::PLYData ply(in);
    happly::Element& vertex = ply.getElement("vertex");
    CHECK((vertex.getProperty<float>("x") == std::vector<float>{1.5f, -0.25f}));
    CHECK((vertex.getProperty<int16_t>("t") == std::vector<int16_t>{-3, 7}));
    CHECK((vertex.getProperty<uint8_t>("id") == std::vector<uint8_t>{200, 9}));
    CHECK((ply.getFaceIndices<uint32_t>() ==
           std::vector<std::vector<uint32_t>>{{0, 1, 1}}));
  }

  context = "foreign crlf";
  {
    std::istringstream in(
        "ply\r\n"
        "format ascii 1.0\r\n"
        "element vertex 2\r\n"
        "property double x\r\n"
        "property list uchar int n\r\n"
        "end_header\r\n"
        "0.5 2 4 5\r\n"
        "-2 0\r\n");
    happly::PLYData ply(in);
    happly::Element& vertex = ply.getElement("vertex");
    CHECK((vertex.getProperty<double>("x") == std::vector<double>{0.5, -2.0}));
    std::vector<std::vector<int>> n(2);
    n[0] = {4, 5};
    CHECK(vertex.getListProperty<int>("n") == n);
  }

  context = "foreign big-endian";
  {
    const char header[] =
        "ply\n"
        "format binary_big_endian 1.0\n"
        "element face 1\n"
        "property list int int vertex_indices\n"
        "element strip 1\n"
        "property list ushort uchar s\n"
        "property ushort w\n"
        "end_header\n";
    const unsigned char payload[] = {
        0, 0, 0, 3, 0, 0, 0, 5, 0xFF, 0xFF, 0xFF, 0xFE, 1, 2, 3, 4,  // face
        0, 2, 7, 8, 0x12, 0x34,                                      // strip
    };
    std::istringstream in(
        std::string(header) +
        std::string(reinterpret_cast<const char*>(payload), sizeof payload));
    happly::PLYData ply(in);
    CHECK((ply.getFaceIndices<int>() ==
           std::vector<std::vector<int>>{{5, -2, 0x01020304}}));
    happly::Element& strip = ply.getElement("strip");
    CHECK((strip.getListProperty<uint8_t>("s") ==
           std::vector<std::vector<uint8_t>>{{7, 8}}));
    CHECK((strip.getProperty<uint16_t>("w") == std::vector<uint16_t>{0x1234}));
  }
}

struct BadHeader {
  const char* text;
  const char* error;
};

void check_errors(const std::string& dir) {
  context = "errors";
  const std::string missing = dir + "/missing.ply";
  CHECK(throws_with([&missing] { happly::PLYData ply(missing); },
                    "Could not open file"));

  const BadHeader bad_headers[] = {
      {"plx\nformat ascii 1.0\nend_header\n", "does not appear to be ply"},
      {"ply\nformat ascii 2.0\nend_header\n", "version != 1.0"},
      {"ply\nformat binary_middle_endian 1.0\nend_header\n", "bad format line"},
      {"ply\nformat ascii 1.0\nelement vertex 1\nproperty int128 x\nend_header\n",
       "cannot be mapped"},
  };
  for (const BadHeader& bad : bad_headers) {
    std::istringstream in(bad.text);
    CHECK(throws_with([&in] { happly::PLYData ply(in); }, bad.error));
  }

  {
    happly::PLYData ply;
    CHECK(throws_with([&ply] { ply.getElement("edge"); }, "no element with name"));
    // Face indices must fit a 32-bit PLY int.
    std::vector<std::vector<int64_t>> wide = {{0, int64_t(1) << 40, 2}};
    CHECK(throws_with([&ply, &wide] { ply.addFaceIndices(wide); },
                      "could not be converted"));
  }

  {
    happly::PLYData ply;
    ply.addElement("vertex", 2);
    happly::Element& vertex = ply.getElement("vertex");
    CHECK(throws_with(
        [&vertex] { vertex.addProperty<float>("x", std::vector<float>(3)); },
        "does not match element"));
    // Lists are written with a uchar count, so 256 entries cannot be stored.
    vertex.addListProperty<uint16_t>(
        "points",
        std::vector<std::vector<uint16_t>>(2, std::vector<uint16_t>(256)));
    std::ostringstream sink;
    CHECK(throws_with(
        [&ply, &sink] { ply.write(sink, happly::DataFormat::Binary); },
        "fit in a uchar"));
  }
}

}  // namespace

int main() {
  std::string dir;
  if (!make_temp_dir(dir)) {
    std::printf("FAIL cannot create a temporary directory\n");
    return 1;
  }
  try {
    const Mesh m = make_mesh();
    round_trip_files(dir, m);
    round_trip_stream(m);
    read_foreign();
    check_errors(dir);
  } catch (const std::exception& e) {
    std::printf("FAIL [%s] unexpected exception: %s\n", context.c_str(),
                e.what());
    ++failures;
  }
  context = "cleanup";
  CHECK(remove_dir(dir));

  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("happly: ok\n");
  return 0;
}
