// Runs a registered CppUnit suite with passing, failing and exception
// tests, then checks the collected results, both outputters, the
// demangled fixture names, and the plug-in library loader.
#include <cppunit/CompilerOutputter.h>
#include <cppunit/Exception.h>
#include <cppunit/TestFailure.h>
#include <cppunit/TestResult.h>
#include <cppunit/TestResultCollector.h>
#include <cppunit/XmlOutputter.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/extensions/TypeInfoHelper.h>
#include <cppunit/plugin/DynamicLibraryManager.h>
#include <cppunit/plugin/DynamicLibraryManagerException.h>

#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>

namespace probe {

class Widget {};

int parse_digit(char c) {
  if (c < '0' || c > '9') {
    throw std::invalid_argument("not a digit");
  }
  return c - '0';
}

class DigitTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(DigitTest);
  CPPUNIT_TEST(parses);
  CPPUNIT_TEST(rejects);
  CPPUNIT_TEST_EXCEPTION(throwsOnLetter, std::invalid_argument);
  CPPUNIT_TEST(deliberatelyFails);
  CPPUNIT_TEST_SUITE_END();

public:
  static int setups;

  void setUp() override { ++setups; }

  void parses() {
    CPPUNIT_ASSERT_EQUAL(7, parse_digit('7'));
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.3, 0.1 + 0.2, 1e-12);
  }

  void rejects() {
    CPPUNIT_ASSERT_THROW(parse_digit('x'), std::invalid_argument);
    CPPUNIT_ASSERT_NO_THROW(parse_digit('0'));
  }

  void throwsOnLetter() { parse_digit('a'); }

  void deliberatelyFails() { CPPUNIT_ASSERT_EQUAL_MESSAGE("sum", 4, 2 + 1); }
};

int DigitTest::setups = 0;

} // namespace probe

CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(probe::DigitTest, "probe");

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static bool contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

int main() {
  CppUnit::Test* suite =
      CppUnit::TestFactoryRegistry::getRegistry("probe").makeTest();
  check(suite->countTestCases() == 4, "four registered tests");

  CppUnit::TestResult controller;
  CppUnit::TestResultCollector collector;
  controller.addListener(&collector);
  suite->run(&controller);

  check(collector.runTests() == 4, "four tests run");
  check(collector.testFailures() == 1, "one assertion failure");
  check(collector.testErrors() == 0, "no errors");
  check(probe::DigitTest::setups == 4, "setUp before each test");

  if (collector.testFailuresTotal() == 1) {
    const CppUnit::TestFailure* failure = collector.failures().front();
    // The fixture name comes from TypeInfoHelper, which demangles it with
    // GCC and Clang and strips "class " with MSVC.
    check(failure->failedTestName() == "probe::DigitTest::deliberatelyFails",
          "failed test name");
    check(!failure->isError(), "failure is an assertion");
    const std::string message = failure->thrownException()->what();
    check(contains(message, "Expected: 4"), "expected value in message");
    check(contains(message, "Actual  : 3"), "actual value in message");
    check(contains(message, "sum"), "user message in message");
  }

  std::ostringstream xml;
  CppUnit::XmlOutputter(&collector, xml).write();
  check(contains(xml.str(),
                 "<Name>probe::DigitTest::deliberatelyFails</Name>"),
        "XML names the failed test");
  check(contains(xml.str(), "<FailuresTotal>1</FailuresTotal>"),
        "XML counts one failure");
  check(contains(xml.str(), "<Name>probe::DigitTest::parses</Name>"),
        "XML lists a successful test");

  std::ostringstream text;
  CppUnit::CompilerOutputter(&collector, text).write();
  check(contains(text.str(), "Failures !!!"), "compiler output reports");
  check(contains(text.str(), "Run: 4"), "compiler output counts");

  check(CppUnit::TypeInfoHelper::getClassName(typeid(probe::Widget)) ==
            "probe::Widget",
        "class name");

  try {
    CppUnit::DynamicLibraryManager missing("cabin-ports-no-such-library");
    check(false, "loading a missing library throws");
  } catch (const CppUnit::DynamicLibraryManagerException& e) {
    check(e.getCause() ==
              CppUnit::DynamicLibraryManagerException::loadingFailed,
          "load failure cause");
    check(contains(e.what(), "cabin-ports-no-such-library"),
          "load failure names the library");
  }

#if defined(_WIN32)
  const char* library = "kernel32.dll";
  const char* symbol = "GetCurrentProcessId";
#elif defined(__APPLE__)
  const char* library = "/usr/lib/libSystem.B.dylib";
  const char* symbol = "getpid";
#elif defined(__linux__) && defined(__GLIBC__)
  const char* library = "libc.so.6";
  const char* symbol = "getpid";
#else
  const char* library = nullptr;
  const char* symbol = nullptr;
#endif
  if (library != nullptr) {
    CppUnit::DynamicLibraryManager system(library);
    check(system.findSymbol(symbol) != nullptr, "symbol from a system library");
    try {
      system.findSymbol("cabin_ports_no_such_symbol");
      check(false, "finding a missing symbol throws");
    } catch (const CppUnit::DynamicLibraryManagerException& e) {
      check(e.getCause() ==
                CppUnit::DynamicLibraryManagerException::symbolNotFound,
            "missing symbol cause");
    }
  }

  delete suite;
  if (failures == 0) {
    std::puts("cppunit: ok");
  }
  return failures == 0 ? 0 : 1;
}
