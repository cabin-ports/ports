#include <dbg.h>

#include <cstdio>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#if DBG_MACRO_CXX_STANDARD >= 17
#include <optional>
#include <string_view>
#endif

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

// dbg() writes "[<file>:<line> (<function>)] <expr> = <value> (<type>)" lines
// to std::cerr, with ANSI colours when it decides the terminal takes them.
class Capture {
 public:
  Capture() : saved_(std::cerr.rdbuf(out_.rdbuf())) {}
  ~Capture() { std::cerr.rdbuf(saved_); }

  std::vector<std::string> Lines() const {
    std::string plain;
    const std::string text = out_.str();
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\x1b') {
        i = text.find('m', i);
        if (i == std::string::npos) break;
      } else {
        plain += text[i];
      }
    }
    std::vector<std::string> lines;
    std::istringstream in(plain);
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
  }

 private:
  std::ostringstream out_;
  std::streambuf* saved_;
};

// The part of a line after its location prefix.
std::string Body(const std::string& line) {
  const std::size_t end = line.find(")] ");
  return end == std::string::npos ? line : line.substr(end + 3);
}

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

int main() {
  std::printf("DBG_MACRO_CXX_STANDARD %d\n", DBG_MACRO_CXX_STANDARD);
  std::vector<std::string> lines;
  int location_line = 0;
  int sum = 0;
  int last = 0;
  {
    Capture capture;
    location_line = __LINE__ + 1;
    sum = dbg(1 + 2);
    std::vector<int> v{1, 2, 3};
    dbg(v);
    std::vector<int> many(12);
    std::iota(many.begin(), many.end(), 0);
    dbg(many);
    std::string s = "cabin";
    dbg(s);
    int a = 4;
    int b = 5;
    last = dbg(a, b);
    dbg(dbg::hex(255));
    lines = capture.Lines();
  }

  check(sum == 3, "dbg() returns its argument");
  check(last == 5, "dbg() with several arguments returns the last");
  check(lines.size() == 7, "one line per argument");
  if (lines.size() == 7) {
    const std::string location =
        ":" + std::to_string(location_line) + " (main)] ";
    check(lines[0][0] == '[' && lines[0].find(location) != std::string::npos,
          "location: " + lines[0]);
    check(Body(lines[0]) == "1 + 2 = 3 (int32_t)", "int: " + lines[0]);
    check(Body(lines[1]) == "v = {1, 2, 3} (std::vector<int32_t>)",
          "vector: " + lines[1]);
    check(Body(lines[2]) ==
              "many = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, ... size:12} "
              "(std::vector<int32_t>)",
          "long vector: " + lines[2]);
    check(Body(lines[3]) == "s = \"cabin\" (std::string)",
          "string: " + lines[3]);
    check(Body(lines[4]) == "a = 4 (int32_t)", "first of two: " + lines[4]);
    check(Body(lines[5]) == "b = 5 (int32_t)", "second of two: " + lines[5]);
    check(StartsWith(Body(lines[6]), "dbg::hex(255) = 0x00FF ("),
          "hex: " + lines[6]);
  }

#if DBG_MACRO_CXX_STANDARD >= 17
  {
    std::vector<std::string> lines17;
    {
      Capture capture;
      std::optional<int> some = 42;
      std::optional<int> none;
      std::string_view view = "view";
      dbg(some, none, view);
      lines17 = capture.Lines();
    }
    check(lines17.size() == 3, "three C++17 lines");
    if (lines17.size() == 3) {
      check(StartsWith(Body(lines17[0]), "some = {42} ("),
            "optional: " + lines17[0]);
      check(StartsWith(Body(lines17[1]), "none = nullopt ("),
            "empty optional: " + lines17[1]);
      check(StartsWith(Body(lines17[2]), "view = \"view\" ("),
            "string_view: " + lines17[2]);
    }
  }
#endif

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("dbg-macro consumer test passed");
  return 0;
}
