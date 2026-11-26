// Exercises Ensure::Always, ToStringWithAllInnerExceptions, the Throw helpers
// and IgnoredExceptions, checking exact messages and the thrown types.

#include <Platform.Exceptions.h>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace PE = Platform::Exceptions;

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Runs fn and returns the invalid_argument message, or "<none>" / "<other>".
template <class F> static std::string invalid_arg_message(F fn) {
  try {
    fn();
  } catch (const std::invalid_argument &e) {
    return e.what();
  } catch (...) {
    return "<other>";
  }
  return "<none>";
}

int main() {
  const auto positive = [](int x) { return x > 3; };

  // ArgumentNotNull
  check(invalid_arg_message([] {
          PE::Ensure::Always::ArgumentNotNull(static_cast<int *>(nullptr), "p");
        }) == "Argument p is null.",
        "ArgumentNotNull null with name");
  check(invalid_arg_message([] {
          PE::Ensure::Always::ArgumentNotNull(static_cast<int *>(nullptr), "p", "need it");
        }) == "Argument p is null: need it.",
        "ArgumentNotNull null with name and message");
  check(invalid_arg_message([] {
          PE::Ensure::Always::ArgumentNotNull(nullptr);
        }) == "Argument is null.",
        "ArgumentNotNull nullptr_t");
  int value = 1;
  check(invalid_arg_message([&] {
          PE::Ensure::Always::ArgumentNotNull(&value, "p");
        }) == "<none>",
        "ArgumentNotNull non-null does not throw");

  // ArgumentMeetsCriteria
  check(invalid_arg_message([&] {
          PE::Ensure::Always::ArgumentMeetsCriteria(5, positive, "x");
        }) == "<none>",
        "ArgumentMeetsCriteria satisfied does not throw");
  check(invalid_arg_message([&] {
          PE::Ensure::Always::ArgumentMeetsCriteria(2, positive, "x", "too small");
        }) == "Invalid x argument: too small.",
        "ArgumentMeetsCriteria with message");
  check(invalid_arg_message([&] {
          PE::Ensure::Always::ArgumentMeetsCriteria(2, positive, "x");
        }) == "Invalid x argument: Argument x does not meet the criteria..",
        "ArgumentMeetsCriteria default message");

  // ToStringWithAllInnerExceptions
  check(PE::ToStringWithAllInnerExceptions(std::runtime_error("boom")) == "boom\n",
        "ToStringWithAllInnerExceptions");

  // Throw helpers
  check(std::string(PE::NotSupportedException().what()) == "Not supported exception.",
        "NotSupportedException");
  check(std::string(PE::NotImplementedException().what()) == "Not implemented exception.",
        "NotImplementedException");
  check(std::string(PE::LinksLimitReachedException().what()) == "Storage links limit has been reached.",
        "LinksLimitReachedException");
  try {
    throw PE::NotSupportedException();
  } catch (const std::logic_error &e) {
    check(std::string(e.what()) == "Not supported exception.", "thrown logic_error");
  }

  // IgnoredExceptions
  PE::Ignore(std::runtime_error("dropped"));
  check(PE::IgnoredExceptions::CollectedExceptions().empty(), "not collected by default");
  PE::IgnoredExceptions::CollectExceptions = true;
  PE::Ignore(std::runtime_error("e"));
  check(PE::IgnoredExceptions::CollectedExceptions().size() == 1, "collected once enabled");
  int seen = 0;
  PE::IgnoredExceptions::ExceptionIgnored += [&seen](void *, const std::exception &) { ++seen; };
  PE::Ignore(std::runtime_error("e2"));
  check(seen == 1 && PE::IgnoredExceptions::CollectedExceptions().size() == 2, "subscriber and bag");

  return failures == 0 ? 0 : 1;
}
