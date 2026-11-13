#include <rapidcsv.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// A fresh directory under the system temporary directory; Remove deletes the
// files handed out by File and then the directory itself.
class TempDir {
 public:
  TempDir() {
#ifdef _WIN32
    char dir[MAX_PATH + 1];
    char name[MAX_PATH];
    // GetTempFileNameA reserves a unique name by creating a file there.
    if (GetTempPathA(MAX_PATH + 1, dir) != 0 &&
        GetTempFileNameA(dir, "csv", 0, name) != 0 && DeleteFileA(name) &&
        CreateDirectoryA(name, nullptr)) {
      path_ = name;
    }
#else
    const char* base = std::getenv("TMPDIR");
    std::string tmpl = (base != nullptr && *base != '\0') ? base : "/tmp";
    tmpl += "/rapidcsv-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (mkdtemp(buf.data()) != nullptr) path_ = buf.data();
#endif
  }

  bool Ok() const { return !path_.empty(); }

  std::string File(const std::string& name) {
    files_.push_back(path_ + "/" + name);
    return files_.back();
  }

  bool Remove() {
    bool ok = true;
    for (const std::string& file : files_) {
      ok = std::remove(file.c_str()) == 0 && ok;
    }
#ifdef _WIN32
    ok = RemoveDirectoryA(path_.c_str()) != 0 && ok;
#else
    ok = rmdir(path_.c_str()) == 0 && ok;
#endif
    return ok;
  }

 private:
  std::string path_;
  std::vector<std::string> files_;
};

std::string ReadBytes(const std::string& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void WriteBytes(const std::string& path, const std::string& bytes) {
  std::ofstream out(path.c_str(), std::ios::binary);
  out << bytes;
}

// CRLF line ends, a quoted field with the separator and a space, doubled
// quotes, and a quoted field holding a CRLF line break.
const std::string kCsv =
    "name,qty,price,note\r\n"
    "apple,3,0.5,\"red, crisp\"\r\n"
    "\"pear \"\"bosc\"\"\",-12,1.25,\"two\r\nlines\"\r\n"
    "kiwi,7,2,plain\r\n";

// ',' separators, no trimming, LF for new documents, and line breaks kept
// inside quoted fields.
rapidcsv::SeparatorParams Separators() {
  return rapidcsv::SeparatorParams(',', false, false, true);
}

// Reads and writes files in dir.  rapidcsv's file I/O throws on failure.
void FileRoundTrips(TempDir& dir) {
  const std::string in_path = dir.File("in.csv");
  WriteBytes(in_path, kCsv);
  rapidcsv::Document from_file(in_path, rapidcsv::LabelParams(0, -1),
                               Separators());
  check(from_file.GetCell<std::string>("note", 1) == "two\r\nlines",
        "file read keeps the quoted line break");
  from_file.SetCell<double>("price", 2, 2.75);
  const std::string edited_path = dir.File("edited.csv");
  from_file.Save(edited_path);
  std::string expected = kCsv;
  expected.replace(expected.find("kiwi,7,2,"), 9, "kiwi,7,2.75,");
  check(ReadBytes(edited_path) == expected, "edited file bytes");

  rapidcsv::Document created(std::string(), rapidcsv::LabelParams(0, -1),
                             Separators());
  created.SetColumnName(0, "id");
  created.SetColumnName(1, "label");
  created.SetColumnName(2, "ratio");
  created.SetCell<int>(0, 0, 1);
  created.SetCell<std::string>(1, 0, "a;b, \"c\"");
  created.SetCell<double>(2, 0, 0.25);
  created.SetCell<int>(0, 1, 2);
  created.SetCell<std::string>(1, 1, "line1\nline2");
  created.SetCell<double>(2, 1, -3.5);
  const std::string created_path = dir.File("created.csv");
  created.Save(created_path);
  check(ReadBytes(created_path) ==
            "id,label,ratio\n"
            "1,\"a;b, \"\"c\"\"\",0.25\n"
            "2,\"line1\nline2\",-3.5\n",
        "created file bytes");

  rapidcsv::Document reread(created_path, rapidcsv::LabelParams(0, -1),
                            Separators());
  check(reread.GetColumn<int>("id") == std::vector<int>{1, 2}, "reread ids");
  check(reread.GetCell<std::string>("label", 0) == "a;b, \"c\"",
        "reread quoted label");
  check(reread.GetCell<std::string>("label", 1) == "line1\nline2",
        "reread line break");
  check(reread.GetColumn<double>("ratio") == std::vector<double>{0.25, -3.5},
        "reread ratios");
}

}  // namespace

int main() {
  std::istringstream in(kCsv);
  rapidcsv::Document doc(in, rapidcsv::LabelParams(0, -1), Separators());

  check(doc.GetRowCount() == 3, "three data rows");
  check(doc.GetColumnCount() == 4, "four columns");
  check(doc.GetColumnNames() ==
            std::vector<std::string>{"name", "qty", "price", "note"},
        "column names");
  check(doc.GetCell<std::string>("note", 0) == "red, crisp",
        "a quoted field keeps its separator");
  check(doc.GetCell<std::string>("name", 1) == "pear \"bosc\"",
        "doubled quotes unescape");
  check(doc.GetCell<std::string>("note", 1) == "two\r\nlines",
        "a quoted field keeps its line break");
  check(doc.GetCell<std::string>("note", 2) == "plain",
        "CRLF line ends leave no CR in the last field");
  check(doc.GetCell<int>("qty", 1) == -12, "int cell");
  check(doc.GetCell<unsigned long>("qty", 2) == 7UL, "unsigned long cell");
  check(doc.GetCell<float>("price", 0) == 0.5f, "float cell");
  check(doc.GetColumn<double>("price") == std::vector<double>{0.5, 1.25, 2.0},
        "double column");
  check(doc.GetColumn<long long>(1) == std::vector<long long>{3, -12, 7},
        "long long column by index");
  check(doc.GetRow<std::string>(2) ==
            std::vector<std::string>{"kiwi", "7", "2", "plain"},
        "string row");

  bool threw = false;
  try {
    doc.GetCell<int>("name", 0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  check(threw, "a word does not convert to int");
  threw = false;
  try {
    doc.GetCell<unsigned long long>("qty", 1);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  check(threw, "a negative number does not convert to unsigned");

  std::istringstream lenient_in("a,b\n1,x\n");
  rapidcsv::Document lenient(lenient_in, rapidcsv::LabelParams(),
                             rapidcsv::SeparatorParams(),
                             rapidcsv::ConverterParams(true, 0.0, -1));
  check(lenient.GetCell<int>("b", 0) == -1,
        "the default converter replaces a bad integer");

  std::istringstream named_in(kCsv);
  rapidcsv::Document named(named_in, rapidcsv::LabelParams(0, 0),
                           Separators());
  check(named.GetCell<int>("qty", "kiwi") == 7, "cell by row name");
  check(named.GetCell<std::string>("note", "pear \"bosc\"") == "two\r\nlines",
        "quoted row name");

  // A read document keeps its CRLF line ends and requotes what needs it.
  std::ostringstream out;
  doc.Save(out);
  check(out.str() == kCsv, "saving reproduces the input");

  TempDir dir;
  check(dir.Ok(), "temporary directory created");
  if (dir.Ok()) {
    try {
      FileRoundTrips(dir);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "FAIL: file round trip threw: %s\n", e.what());
      ++failures;
    }
    check(dir.Remove(), "temporary files and directory removed");
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("rapidcsv consumer test passed");
  return 0;
}
