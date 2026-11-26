// Consumer: tfm::format into a string and a stream, tfm::printf and printfln.
// Expected strings follow tinyformat_test.cpp (cited per group).
#include <tinyformat.h>

#include <cstddef>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(int line, const std::string& got, const std::string& want) {
  if (got != want) {
    std::printf("main.cc:%d: got '%s', want '%s'\n", line, got.c_str(), want.c_str());
    ++failures;
  }
}

#define CHECK_EQ(got, want) check(__LINE__, (got), (want))

}  // namespace

int main() {
  // README.md:59 example, with %s of std::string and const char*.
  CHECK_EQ(tfm::format("%s, %s %d, %.2d:%.2d", std::string("Wednesday"), "July", 27, 14, 44),
           "Wednesday, July 27, 14:44");
  // Positional arguments (as tinyformat_test.cpp:178, with %s).
  CHECK_EQ(tfm::format("%2$s %1$s", "a", "b"), "b a");
  // Flags, width, precision (tinyformat_test.cpp:221, 226, 230, 235).
  CHECK_EQ(tfm::format("%+d|%+.2d|%-010d|", 3, -3, 10), "+3|-03|10        |");
  CHECK_EQ(tfm::format("%05d|%5.2f", 42, 3.14159), "00042| 3.14");
  // Hex (tinyformat_test.cpp:120-121, 218, 226; %x and %X as there).
  CHECK_EQ(tfm::format("%x|%X|%#x|%#010X", 255, 0xBEEF, 0x271828, 0xBEEF),
           "ff|BEEF|0x271828|0X0000BEEF");
  // %d of std::size_t (tinyformat_test.cpp:243 uses %zd).
  CHECK_EQ(tfm::format("%d", std::size_t(7)), "7");
  CHECK_EQ(tfm::format("%zd", std::size_t(100000)), "100000");
  // %% (tinyformat_test.cpp:141).
  CHECK_EQ(tfm::format("%%%s", "asdf"), "%asdf");

  // Into a stream, independent of its state (tinyformat_test.cpp:294-300).
  std::ostringstream oss;
  oss.width(20);
  oss.precision(10);
  oss.fill('*');
  oss.setf(std::ios::scientific);
  tfm::format(oss, "%f", 10.1234123412341234);
  CHECK_EQ(oss.str(), "10.123412");

  // printf and printfln write to std::cout (tinyformat_test.cpp:316-322).
  std::ostringstream capture;
  std::streambuf* old = std::cout.rdbuf(capture.rdbuf());
  tfm::printf("%s %s %d\n", "printf", "test", 1);
  tfm::printfln("%s %s %d", "printfln", "test", 1);
  std::cout.rdbuf(old);
  CHECK_EQ(capture.str(), "printf test 1\nprintfln test 1\n");

  return failures == 0 ? 0 : 1;
}
