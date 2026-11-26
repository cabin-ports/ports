// Parses CSV from memory and from a file, checks values, types and column
// names, and compares the writer's output byte for byte.

#include <csv.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static const char *const kText =
    "name,note,qty,price\n"
    "foo,\"a, b\nc\",3,1.5\n"
    "bar,plain,-4,2.25\n"
    "baz,\"say \"\"hi\"\"\",0,10\n";

// Parses the document and checks everything about it.
static void check_rows(csv::CSVReader &reader, const char *label) {
  const std::vector<std::string> cols = reader.get_col_names();
  check(cols.size() == 4 && cols[0] == "name" && cols[1] == "note" &&
            cols[2] == "qty" && cols[3] == "price",
        label);

  const char *names[] = {"foo", "bar", "baz"};
  const char *notes[] = {"a, b\nc", "plain", "say \"hi\""};
  const int qtys[] = {3, -4, 0};
  const double prices[] = {1.5, 2.25, 10.0};
  size_t n = 0;
  for (csv::CSVRow &row : reader) {
    if (n == 3) {
      check(false, "more than three rows");
      break;
    }
    check(row.size() == 4, "row width");
    check(row["name"].get<std::string>() == names[n], "name");
    check(row["note"].get<std::string>() == notes[n], "quoted note");
    check(row["note"].is_str(), "note is a string");
    check(row["qty"].is_int(), "qty is an int");
    check(row["qty"].get<int>() == qtys[n], "qty value");
    check(row["price"].is_float() || row["price"].is_int(), "price is numeric");
    check(row["price"].get<double>() == prices[n], "price value");
    ++n;
  }
  check(n == 3, "row count");
  check(reader.n_rows() == 3, "n_rows");
}

int main() {
  csv::CSVReader memory = csv::parse(kText);
  check_rows(memory, "memory column names");

  // The first row of the document, and the quoted field's escapes, in one go.
  csv::CSVReader again = csv::parse(kText);
  csv::CSVRow first = *again.begin();
  check(first["note"].get_sv() == "a, b\nc", "first row note view");
  check(first[2].get<long long>() == 3, "positional access");

  const std::string expected =
      "name,note,qty,price\n"
      "foo,\"a, b\nc\",3,1.5\n"
      "bar,plain,-4,2.25\n"
      "baz,\"say \"\"hi\"\"\",0,10\n";
  std::stringstream out;
  {
    csv::CSVWriter<std::stringstream> writer = csv::make_csv_writer(out);
    std::vector<std::string> header;
    header.push_back("name");
    header.push_back("note");
    header.push_back("qty");
    header.push_back("price");
    writer << header;
    writer << std::make_tuple(std::string("foo"), std::string("a, b\nc"), 3, std::string("1.5"));
    writer << std::make_tuple(std::string("bar"), std::string("plain"), -4, std::string("2.25"));
    writer << std::make_tuple(std::string("baz"), std::string("say \"hi\""), 0, std::string("10"));
  }
  check(out.str() == expected, "writer bytes");

  // File round trip: write with the writer, read back by file name (mmap path).
  const char* tmp = std::getenv("TMPDIR");
  if (!tmp) tmp = std::getenv("TEMP");  // Windows
  const std::string path = std::string(tmp ? tmp : "/tmp") + "/cabin-csv-parser-test.csv";
  {
    std::ofstream file(path.c_str(), std::ios::binary);
    file << out.str();
    check(file.good(), "temporary file written");
  }
  {
    csv::CSVReader from_file(path);
    check_rows(from_file, "file column names");
  }
  std::remove(path.c_str());

  if (failures != 0) return 1;
  std::puts("ok");
  return 0;
}
