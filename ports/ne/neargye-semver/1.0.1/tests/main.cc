// Consumer test for cabin-ports/neargye-semver: strict parsing and its error
// reporting, SemVer 2.0.0 precedence, transformations, serialization, and the
// range syntax with its prerelease policy, following doc/reference.md.
#include <semver.hpp>

#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void test_parsing() {
  const auto v = semver::from_string("1.2.3");
  check(v.major() == 1 && v.minor() == 2 && v.patch() == 3, "from_string components");
  check(v.to_string() == "1.2.3", "to_string");

  semver::version<> full;
  const auto result = semver::parse("1.2.3-alpha+build.7", full);
  check(static_cast<bool>(result) && full.prerelease_tag() == "alpha" && full.build_metadata() == "build.7",
        "parse keeps the prerelease tag and build metadata");

  const std::string_view bad = "1.2.x";
  semver::version<> untouched{9, 9, 9};
  const auto failed = semver::parse(bad, untouched);
  check(!failed && failed.ec == std::errc::invalid_argument && failed.ptr == bad.data() + 4 && untouched.major() == 9,
        "a failed parse points at the bad byte and preserves its output");

  check(!semver::try_parse("1.2").has_value(), "strict parsing needs MAJOR.MINOR.PATCH");
  check(!semver::valid("01.2.3") && semver::valid("1.2.3-rc.1"), "leading zeros are invalid");

  bool threw = false;
  try {
    (void)semver::from_string("x.y");
  } catch (const std::system_error &) {
    threw = true;
  }
  check(threw, "from_string throws std::system_error");

  const auto coerced = semver::coerce("v1.0");
  check(coerced && *coerced == semver::version<>(1, 0, 0), "coerce fills missing components");
  const auto cleaned = semver::clean(" v1.2.3 ");
  check(cleaned && *cleaned == semver::version<>(1, 2, 3) && !semver::clean("1.2"), "clean strips wrappers only");

  // from_chars takes the longest valid prefix.
  const char *text = "1.2.3-rc.1 tail";
  semver::version<> prefix;
  const auto read = semver::from_chars(text, text + std::strlen(text), prefix);
  check(static_cast<bool>(read) && read.ptr == text + 10 && prefix.to_string() == "1.2.3-rc.1", "from_chars");
}

static void test_precedence() {
  // The ordering example of SemVer 2.0.0, section 11.
  const char *const ordered[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                                 "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"};
  for (std::size_t i = 0; i + 1 < sizeof ordered / sizeof ordered[0]; ++i)
    check(semver::from_string(ordered[i]) < semver::from_string(ordered[i + 1]), "SemVer 2.0.0 precedence");

  const auto a = semver::from_string("1.2.3+ci.1"), b = semver::from_string("1.2.3+ci.2");
  check(a == b && semver::compare(a, b) == 0, "build metadata does not affect precedence");
  check(semver::compare_with_build(a, b) == -1, "compare_with_build orders by build metadata");
  check(semver::diff(semver::from_string("1.2.3"), semver::from_string("1.3.0")) == semver::version_change::minor,
        "diff reports the first differing component");
  check(std::hash<semver::version<>>{}(a) == std::hash<semver::version<>>{}(b), "the hash ignores build metadata");
}

static void test_transformations_and_output() {
  const semver::version<> current{1, 2, 3, "rc.1", "ci"};
  check(current.bump_minor().to_string() == "1.3.0", "bump_minor resets patch and drops qualifiers");
  check(current.without_prerelease().to_string() == "1.2.3+ci", "without_prerelease keeps build metadata");
  check(current.without_build_metadata().to_string() == "1.2.3-rc.1", "without_build_metadata keeps the prerelease");

  const auto next_pre = semver::inc(current, semver::version_change::prerelease);
  check(next_pre && next_pre->to_string() == "1.2.3-rc.2", "inc bumps the final numeric prerelease identifier");
  const auto next_patch = semver::inc(current, semver::version_change::patch);
  check(next_patch && next_patch->to_string() == "1.2.4", "inc patch releases the next patch");
  const auto first_pre = semver::inc(semver::version<>{1, 2, 3}, semver::version_change::prerelease);
  check(first_pre && first_pre->to_string() == "1.2.4-0", "inc prerelease on a release");
  check(!semver::inc(current, semver::version_change::none), "inc rejects none");

  char buffer[32];
  const auto written = semver::to_chars(buffer, buffer + sizeof buffer, current);
  check(static_cast<bool>(written) && std::string(buffer, written.ptr) == "1.2.3-rc.1+ci", "to_chars");
  char tiny[4];
  check(semver::to_chars(tiny, tiny + sizeof tiny, current).ec == std::errc::value_too_large,
        "to_chars rejects a short buffer");

  std::ostringstream out;
  out << current;
  check(out.str() == "1.2.3-rc.1+ci", "stream insertion");
}

static void test_ranges() {
  const auto caret = semver::try_parse_range("^1.2");
  check(caret.has_value(), "^1.2 parses");
  if (caret) {
    check(caret->contains(semver::from_string("1.2.3")), "^1.2 contains 1.2.3");
    check(!caret->contains(semver::from_string("2.0.0")), "^1.2 excludes 2.0.0");
    check(!caret->contains(semver::from_string("1.5.0-beta")), "prereleases are excluded by default");
    check(caret->contains(semver::from_string("1.5.0-beta"), semver::prerelease_policy::include),
          "prerelease_policy::include admits them");
  }

  const auto set = semver::try_parse_range(" >=1.2 <2 || ~3.1 ");
  check(set.has_value(), "a range set parses");
  if (set) {
    check(set->contains(semver::version<>{1, 5, 0}), ">=1.2 <2 contains 1.5.0");
    check(!set->contains(semver::version<>{2, 0, 0}), "the set excludes 2.0.0");
    check(set->contains(semver::version<>{3, 1, 4}) && !set->contains(semver::version<>{3, 2, 0}), "~3.1");
    const auto lowest = semver::min_version(*set);
    check(lowest && *lowest == semver::version<>(1, 2, 0), "min_version");
    if (caret) {
      const auto above = semver::try_parse_range(">=2");
      check(semver::intersects(*caret, *set), "^1.2 intersects the set");
      check(above && !semver::intersects(*caret, *above), "^1.2 and >=2 are disjoint");
    }
  }

  check(!semver::try_parse_range("^1.x").has_value(), "a wildcard operand of ^ is invalid");
  check(semver::satisfies(semver::from_string("0.2.5"), "^0.2.3") &&
            !semver::satisfies(semver::from_string("0.3.0"), "^0.2.3"),
        "caret on a zero major");
}

int main() {
  test_parsing();
  test_precedence();
  test_transformations_and_output();
  test_ranges();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("neargye-semver 1.0.1 C++ ok");
  return 0;
}
