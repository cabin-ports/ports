#include <semver/version.hh>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

// Built in place: a copy's pre-release identifiers point into the original.
using pv = semver::project_version;

// The values configure_file writes into version.hh, at compile time.
static_assert(SEMVER_VERSION_MAJOR == 0 && SEMVER_VERSION_MINOR == 1 &&
                  SEMVER_VERSION_PATCH == 1,
              "version.hh numbers");
static_assert(semver::version.get_major() == 0 &&
                  semver::version.get_minor() == 1 &&
                  semver::version.get_patch() == 1 &&
                  semver::version.get_prerelease().size() == 1,
              "semver::version parses at compile time");

}  // namespace

int main() {
  check(std::string(SEMVER_NAME) == "semver" &&
            std::string(SEMVER_VERSION_STABILITY) == "-beta",
        "version.hh strings");
  check(semver::version.to_string() == "0.1.1-beta", "semver::version");
  check(semver::get_version() == semver::version,
        "the library's get_version() matches the header");

  const char *text = "1.3.0-beta+exp.sha.5114f85";
  semver::project_version full{text};
  check(full.get_major() == 1 && full.get_minor() == 3 &&
            full.get_patch() == 0,
        "major, minor and patch parse");
  check(full.get_prerelease().size() == 1 &&
            full.get_prerelease().begin()->get_alnum() == "beta",
        "the pre-release identifier parses");
  check(full.get_build() == "exp.sha.5114f85", "build metadata parses");
  check(full.to_string() == text, "to_string round-trips");

  // SemVer 2.0.0 section 11 precedence, where 0.1.1 implements it.
  check(pv{"1.0.0"} < pv{"2.0.0"} && pv{"2.0.0"} < pv{"2.1.0"},
        "major, then minor");
  check(pv{"1.0.0-alpha"} < pv{"1.0.0"}, "a pre-release precedes the release");
  check(pv{"1.0.0-alpha"} < pv{"1.0.0-beta"}, "identifiers compare lexically");
  check(pv{"1.0.0-alpha"} < pv{"1.0.0-alpha.beta"},
        "more identifiers take precedence");
  check(pv{"1.0.0-alpha.1"} < pv{"1.0.0-alpha.beta"},
        "numeric identifiers precede alphanumeric ones");
  check(pv{"1.0.0+a"} == pv{"1.0.0+b"}, "build metadata is ignored");

  check(pv{"1.2.0"}.compatible_with(pv{"1.3.0"}), "1.2.0 runs on 1.3.0");
  check(!pv{"1.3.0"}.compatible_with(pv{"1.2.0"}),
        "1.3.0 needs at least 1.3.0");
  check(!pv{"1.9.0"}.compatible_with(pv{"2.0.0"}), "a new major breaks");
  check(pv{"0.1.0"}.compatible_with(pv{"0.1.0"}) &&
            !pv{"0.1.0"}.compatible_with(pv{"0.1.1"}),
        "0.y.z needs the same version");

  check(semver::component::parse("42").holds_number() &&
            !semver::component::parse("rc1").holds_number(),
        "component::parse");

  // The defects cabin.toml records.
  check(pv{"2.0.5"} < pv{"2.0.3"}, "a differing patch compares against major");
  check(pv{"1.0.0-rc.5"}.to_string() == "1.0.0-rc.1",
        "a numeric identifier prints as 0 or 1");

  if (failures != 0) {
    return 1;
  }
  std::printf("mbits-semver %s: %s parses and orders\n",
              semver::get_version().to_string().c_str(), text);
  return 0;
}
