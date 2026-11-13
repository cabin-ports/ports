// A C++11 consumer of argh (a C++-only library): flags, parameters and
// positional arguments under each parse mode, registered parameters, typed
// extraction with and without defaults, and re-parsing.
#include <argh.h>

#include <cstddef>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

template <std::size_t N>
int argc_of(const char* const (&)[N]) {
  return static_cast<int>(N);
}

// The default mode: an unregistered option followed by a value is a flag,
// and the value stays positional.
void test_prefer_flag() {
  const char* const argv[] = {"prog",  "-v",  "--name=world", "--count",
                              "42",    "-5",  "input.txt",    "-v"};
  argh::parser cmdl(argc_of(argv), argv);

  CHECK(cmdl["v"]);
  CHECK(cmdl["--v"]);
  CHECK(cmdl.flags().count("v") == 2);
  CHECK((cmdl[{"-c", "--count"}]));
  CHECK(!cmdl["missing"]);
  CHECK(cmdl("name").str() == "world");
  CHECK(cmdl.params().size() == 1);

  CHECK(cmdl.size() == 4);
  CHECK(cmdl[0] == "prog");
  CHECK(cmdl[3] == "input.txt");
  CHECK(cmdl[9].empty());
  int number = 0;
  CHECK(cmdl(1) >> number);
  CHECK(number == 42);
  CHECK(cmdl(2) >> number);
  CHECK(number == -5);
  CHECK(!(cmdl(9) >> number));
  double scale = 0;
  CHECK(cmdl(9, 2.5) >> scale);
  CHECK(scale == 2.5);
  std::string name;
  cmdl(0) >> name;
  CHECK(name == "prog");

  std::string joined;
  for (const std::string& arg : cmdl) joined += arg + ",";
  CHECK(joined == "prog,42,-5,input.txt,");
}

// An unregistered option takes the next argument as its value.
void test_prefer_param() {
  const char* const argv[] = {"prog", "--count", "42", "-t", "0.75",
                              "-q",   "--bad",   "x1", "file"};
  argh::parser cmdl(argc_of(argv), argv,
                    argh::parser::PREFER_PARAM_FOR_UNREG_OPTION);

  int n = 0;
  CHECK(cmdl("count") >> n);
  CHECK(n == 42);
  CHECK(cmdl({"-c", "--count"}) >> n);
  CHECK(n == 42);
  double threshold = 0;
  CHECK(cmdl({"-t", "--threshold"}) >> threshold);
  CHECK(threshold == 0.75);
  // -q is followed by another option, so it is a flag.
  CHECK(cmdl["q"]);
  CHECK(!(cmdl("bad") >> n));
  CHECK(cmdl("bad").str() == "x1");
  CHECK(!(cmdl("missing") >> n));
  int fallback = 0;
  CHECK(cmdl("missing", 7) >> fallback);
  CHECK(fallback == 7);
  double ratio = 0;
  CHECK(cmdl("ratio", 0.1) >> ratio);
  CHECK(ratio == 0.1);
  CHECK(cmdl({"-z", "--zz"}, "none").str() == "none");
  CHECK(cmdl.size() == 2);
  CHECK(cmdl[1] == "file");
}

// Registered parameters take a value even in the default mode, and
// re-parsing replaces the previous result.
void test_registered() {
  argh::parser cmdl({"-o", "--output"});
  cmdl.add_param("level");
  const char* const argv[] = {"prog", "-o", "out.bin", "--level", "3",
                              "--fast", "src.c"};
  cmdl.parse(argc_of(argv), argv);

  CHECK(cmdl({"o", "output"}).str() == "out.bin");
  int level = 0;
  CHECK(cmdl("level") >> level);
  CHECK(level == 3);
  CHECK(cmdl["fast"]);
  CHECK(cmdl.size() == 2);
  CHECK(cmdl[1] == "src.c");

  const char* const again[] = {"tool", "--output", "x.bin", nullptr};
  cmdl.parse(again);
  CHECK(cmdl("output").str() == "x.bin");
  CHECK(!cmdl("o"));
  CHECK(!cmdl["fast"]);
  CHECK(cmdl.size() == 1);
  CHECK(cmdl[0] == "tool");
}

void test_multiflag_and_no_split() {
  argh::parser tar;
  tar.add_param("f");
  const char* const argv[] = {"tar", "-xvf", "archive.tar", "-abc"};
  tar.parse(argc_of(argv), argv, argh::parser::SINGLE_DASH_IS_MULTIFLAG);
  CHECK(tar["x"] && tar["v"] && tar["a"] && tar["b"] && tar["c"]);
  CHECK(tar.flags().size() == 5);
  CHECK(tar("f").str() == "archive.tar");
  CHECK(tar.size() == 1);

  const char* const defines[] = {"cc", "--define=A=1"};
  argh::parser cc(argc_of(defines), defines, argh::parser::NO_SPLIT_ON_EQUALSIGN);
  CHECK(cc["define=A=1"]);
  CHECK(cc.params().empty());

  argh::parser split(argc_of(defines), defines);
  CHECK(split("define").str() == "A=1");
}

}  // namespace

int main() {
  test_prefer_flag();
  test_prefer_param();
  test_registered();
  test_multiflag_and_no_split();
  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("argh: ok\n");
  return 0;
}
