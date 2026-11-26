#include <vtu11/vtu11.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
  if (!ok)
  {
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
  }
}

static std::string slurp(const std::string& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static bool has(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

// Decodes standard base64 with '=' padding.
static std::string base64Decode(const std::string& text)
{
  const std::string alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  unsigned bits = 0;
  int count = 0;

  for (char c : text)
  {
    if (c == '=') break;
    bits = (bits << 6) | static_cast<unsigned>(alphabet.find(c));
    count += 6;
    if (count >= 8)
    {
      count -= 8;
      out.push_back(static_cast<char>((bits >> count) & 0xff));
    }
  }
  return out;
}

int main()
{
  // Two tetrahedra sharing a face, with one point and one cell data set.
  std::vector<double> points{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1};
  std::vector<vtu11::VtkIndexType> connectivity{0, 1, 2, 3, 1, 2, 3, 4};
  std::vector<vtu11::VtkIndexType> offsets{4, 8};
  std::vector<vtu11::VtkCellType> types{10, 10};
  vtu11::Vtu11UnstructuredMesh mesh{points, connectivity, offsets, types};

  std::vector<double> pointData{1.5, 2.5, 3.5, 4.5, 5.5};
  std::vector<double> cellData{10, 20};
  std::vector<vtu11::DataSetInfo> info{
    vtu11::DataSetInfo{"p", vtu11::DataSetType::PointData, 1},
    vtu11::DataSetInfo{"c", vtu11::DataSetType::CellData, 1}};
  std::vector<vtu11::DataSetData> data{pointData, cellData};

  // Everything is written under a fresh directory inside the temp directory.
  const std::string dir =
    (vtu11fs::temp_directory_path() / "vtu11-consumer-test").string();
  vtu11fs::remove_all(dir);
  vtu11fs::create_directories(dir);
  const std::string ascii = dir + "/ascii.vtu";
  const std::string base64 = dir + "/base64.vtu";
  const std::string raw = dir + "/raw.vtu";
  const std::string packed = dir + "/compressed.vtu";

  vtu11::writeVtu(ascii, mesh, info, data, "Ascii");
  vtu11::writeVtu(base64, mesh, info, data, "Base64Inline");
  vtu11::writeVtu(raw, mesh, info, data, "RawBinary");
  vtu11::writeVtu(packed, mesh, info, data, "RawBinaryCompressed");

  // Counts.
  const std::string a = slurp(ascii);
  check(has(a, "<Piece NumberOfCells=\"2\" NumberOfPoints=\"5\">"), "ascii piece counts");
  check(has(slurp(base64), "<Piece NumberOfCells=\"2\" NumberOfPoints=\"5\">"),
        "base64 piece counts");

  // Ascii values.
  check(has(a, "<DataArray Name=\"p\" format=\"ascii\" type=\"Float64\">\n1.5 2.5 3.5 4.5 5.5 \n</DataArray>"),
        "ascii point data");
  check(has(a, "<DataArray Name=\"c\" format=\"ascii\" type=\"Float64\">\n10 20 \n</DataArray>"),
        "ascii cell data");
  check(has(a, "<DataArray Name=\"connectivity\" format=\"ascii\" type=\"Int64\">\n0 1 2 3 1 2 3 4 \n"),
        "ascii connectivity");

  // Base64: a UInt64 byte count, then the doubles, each encoded on its own.
  const std::string b = slurp(base64);
  check(has(b, "header_type=\"UInt64\""), "base64 header type");
  const std::string tag = "<DataArray Name=\"p\" format=\"binary\" type=\"Float64\">\n";
  const std::string::size_type at = b.find(tag);
  check(at != std::string::npos, "base64 point data array");
  if (at != std::string::npos)
  {
    const std::string line = b.substr(at + tag.size(), b.find('\n', at + tag.size()) - at - tag.size());
    check(line.compare(0, 12, "KAAAAAAAAAA=") == 0, "base64 byte count header (40)");
    const std::string payload = base64Decode(line.substr(12));
    check(payload.size() == 40, "base64 payload size");
    double decoded[5] = {};
    if (payload.size() == 40) std::memcpy(decoded, payload.data(), 40);
    check(decoded[0] == 1.5 && decoded[4] == 5.5, "base64 payload values");
  }

  // Compressed raw: zlib's compressor attribute and a zlib stream after the block header.
  const std::string z = slurp(packed);
  check(has(z, "compressor=\"vtkZLibDataCompressor\""), "compressor attribute");
  check(!has(slurp(raw), "compressor="), "uncompressed raw has no compressor");
  const std::string::size_type underscore = z.find("<AppendedData encoding=\"raw\">\n_");
  check(underscore != std::string::npos, "appended data section");
  if (underscore != std::string::npos)
  {
    std::uint64_t head[3] = {};
    const std::string appended = z.substr(underscore + 31);
    std::memcpy(head, appended.data(), sizeof(head));
    check(head[0] == 1 && head[1] == 32768 && head[2] == 40, "compressed block header");
    check(static_cast<unsigned char>(appended[32]) == 0x78, "zlib stream magic");
  }

  // Invalid mode.
  bool threw = false;
  try { vtu11::writeVtu(dir + "/bad.vtu", mesh, info, data, "nope"); }
  catch (const std::runtime_error&) { threw = true; }
  check(threw, "invalid write mode throws");

  // pvtu, which goes through the filesystem alias.
  vtu11::writePVtu(dir, "parts", info, 2);
  const std::string pv = slurp(dir + "/parts.pvtu");
  check(has(pv, "<Piece Source=\"parts/parts_0.vtu\"/>") && has(pv, "<Piece Source=\"parts/parts_1.vtu\"/>"),
        "pvtu pieces");
  check(vtu11fs::is_directory(dir + "/parts"), "pvtu directory created");

  vtu11fs::remove_all(dir);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
