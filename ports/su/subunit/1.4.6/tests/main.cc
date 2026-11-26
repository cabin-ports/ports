// Runs a CppUnit suite through the subunit listener and compares the
// protocol lines it writes to stdout.
#include <cppunit/TestResult.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <subunit/SubunitTestProgressListener.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace probe {

class T : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(T);
  CPPUNIT_TEST(ok1);
  CPPUNIT_TEST(bad);
  CPPUNIT_TEST(boom);
  CPPUNIT_TEST(ok2);
  CPPUNIT_TEST_SUITE_END();

public:
  void ok1() { CPPUNIT_ASSERT_EQUAL(4, 2 + 2); }
  void bad() { CPPUNIT_ASSERT_EQUAL_MESSAGE("sum", 4, 2 + 1); }
  void boom() { throw std::runtime_error("kaboom"); }
  void ok2() { CPPUNIT_ASSERT(true); }
};

} // namespace probe

CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(probe::T, "probe");

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// The next needle must appear at or after the previous one.
static void expect_in_order(const std::string& out, const char* needle,
                            std::size_t& pos) {
  std::size_t at = out.find(needle, pos);
  check(at != std::string::npos, needle);
  if (at != std::string::npos) {
    pos = at + std::string(needle).size();
  }
}

int main() {
  CppUnit::Test* suite =
      CppUnit::TestFactoryRegistry::getRegistry("probe").makeTest();
  CppUnit::TestResult controller;
  CppUnit::SubunitTestProgressListener listener;
  controller.addListener(&listener);

  std::fflush(stdout);
  int saved = dup(fileno(stdout));
  // Among the probe's build outputs: MSVC creates tmpfile() files in the drive root.
  const char* dir = std::getenv("CABIN_PROFILE_DIR");
  std::FILE* in = dir == nullptr ? nullptr
      : std::fopen((std::string(dir) + "/port-probe-subunit.tmp").c_str(), "w+b");
  if (saved < 0 || in == nullptr || dup2(fileno(in), fileno(stdout)) < 0) {
    std::fprintf(stderr, "FAIL: cannot redirect stdout\n");
    return 1;
  }
  suite->run(&controller);
  std::fflush(stdout);
  dup2(saved, fileno(stdout));
  close(saved);
  delete suite;

  std::string out;
  std::rewind(in);
  char buf[512];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, in)) > 0) {
    out.append(buf, n);
  }
  std::fclose(in);

  check(out.rfind("test: probe::T::ok1\nsuccess: probe::T::ok1\n"
                  "test: probe::T::bad\nfailure: probe::T::bad [\n",
                  0) == 0,
        "passing test, then the start and failure of the next");
  std::size_t pos = 0;
  expect_in_order(out, "Expected: 4", pos);
  expect_in_order(out, "Actual  : 3", pos);
  expect_in_order(out, "sum", pos);
  expect_in_order(out, "]\ntest: probe::T::boom\nerror: probe::T::boom [\n", pos);
  expect_in_order(out, "kaboom", pos);
  // A failed test is not also reported as passed; the next one is.
  expect_in_order(out, "]\ntest: probe::T::ok2\nsuccess: probe::T::ok2\n", pos);
  check(out.find("success: probe::T::bad") == std::string::npos &&
            out.find("success: probe::T::boom") == std::string::npos,
        "no success after a failure");
  const std::string tail = "success: probe::T::ok2\n";
  check(out.size() >= tail.size() &&
            out.compare(out.size() - tail.size(), tail.size(), tail) == 0,
        "nothing after the last test");

  if (failures == 0) {
    std::puts("cppunit_subunit: ok");
  }
  return failures == 0 ? 0 : 1;
}
