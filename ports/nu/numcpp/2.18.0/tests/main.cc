// Consumer test for cabin-ports/numcpp: NdArray, linalg, number theory,
// special functions (Boost.Math), random (Boost.Random) and nc::logger
// (Boost.Log).
#include "NumCpp.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

int failures = 0;

void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

} // namespace

int main() {
  nc::NdArray<int> a = {{1, 2}, {3, 4}};
  check(a.shape().rows == 2 && a.shape().cols == 2, "shape");
  check(a.sum().item() == 10, "sum");
  check(nc::linalg::det(a) == -2, "det");
  check(a.transpose()(0, 1) == 3, "transpose");

  const auto sq = nc::dot(a, a);
  check(sq(0, 0) == 7 && sq(0, 1) == 10 && sq(1, 0) == 15 && sq(1, 1) == 22,
        "dot");

  check(nc::gcd(12, 18) == 6, "gcd");
  check(nc::lcm(4, 6) == 12, "lcm");
  check(nc::arange<int>(0, 5).sum().item() == 10, "arange");

  check(std::fabs(nc::special::gamma(5.0) - 24.0) < 1e-12, "special::gamma");
  check(std::fabs(nc::special::factorial(5) - 120.0) < 1e-12,
        "special::factorial");

  const nc::NdArray<double> b = {{4., 1.}, {2., 3.}};
  const auto inv = nc::linalg::inv(b) * 10.;
  check(std::fabs(inv(0, 0) - 3.) < 1e-12 && std::fabs(inv(0, 1) + 1.) < 1e-12 &&
            std::fabs(inv(1, 0) + 2.) < 1e-12 &&
            std::fabs(inv(1, 1) - 4.) < 1e-12,
        "linalg::inv");

  const auto laplace = nc::random::laplace<double>(nc::Shape(1, 1000), 0., 1.);
  check(laplace.size() == 1000 && std::fabs(nc::mean(laplace).item()) < 0.5,
        "random::laplace");

  nc::logger::fileLogger::get(); // creates the console sink setLogLevel needs
  const auto logBase =
      std::filesystem::temp_directory_path() / "numcpp-consumer-test";
  nc::logger::addOutputFileLog(logBase);
  nc::logger::setLogLevel(boost::log::trivial::warning);
  LOG_INFO << "dropped";
  LOG_WARNING << "kept " << 42;
  nc::logger::detail::sinkFile->flush();
  const auto logFile = std::filesystem::path(logBase).replace_extension("log");
  std::ifstream in(logFile);
  const std::string log((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  check(log.find("[warning]") != std::string::npos &&
            log.find("kept 42") != std::string::npos,
        "logger writes warning");
  check(log.find("dropped") == std::string::npos, "logger filters info");
  in.close();
  // The sink keeps the file open, which can make removal fail on Windows.
  std::error_code ec;
  std::filesystem::remove(logFile, ec);

  if (failures == 0) {
    std::puts("numcpp consumer test passed");
  }
  return failures == 0 ? 0 : 1;
}
