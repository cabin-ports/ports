#define clue_LOG_TO_STRING
#define clue_LOG_MODULE_NAME "mod: "
#define clue_LOG_LEVEL clue_LOG_SEV_WARNING
#define clue_LOG_LEVEL_BUILD clue_LOG_SEV_WARNING
#include <clue.hpp>

#include <cstdio>

using clue::the_log;  // the string destination's macros call it unqualified

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  clue::strlog &log = the_log();

  check(std::string(clue_VERSION) == "1.0.0", "version");

  check(clue::to_severity_text(clue_LOG_SEV_ERROR) == "Error", "severity text");
  check(clue::to_severity_text(clue_LOG_SEV_NONE) == "[clue]", "none text");
  check(clue::to_severities_text(2) == "Emergency, Alert, Critical.",
        "severities text");

  // Module name prefixes the message; the severity is recorded.
  clue_LOG_ERROR("disk " << 42 << " full");
  check(log.text() == "mod: disk 42 full", "error text");
  check(log.severity() == clue_LOG_SEV_ERROR, "error severity");

  log.clear();
  check(log.text().empty() && log.severity() == clue_LOG_SEV_NONE, "clear");

  clue_LOG_WARNING("w");
  check(log.text() == "mod: w" && log.severity() == clue_LOG_SEV_WARNING,
        "warning at the level");

  // Info is above the level: nothing is logged and nothing changes.
  clue_LOG_INFO("hidden");
  check(log.text() == "mod: w" && log.severity() == clue_LOG_SEV_WARNING,
        "info filtered");

  log.clear();
  clue_LOG_LOGGED_SEVERITIES();
  check(log.text() == "mod: Emergency, Alert, Critical, Error, Warning.",
        "logged severities");
  check(log.severity() == clue_LOG_SEV_NONE, "logged severities severity");

  return failures;
}
