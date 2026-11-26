#include <logfault/logfault.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

// Records what the manager delivers, and each message's level.
struct Capture : logfault::Handler {
  explicit Capture(logfault::LogLevel level) : Handler(level) {}

  void LogMessage(const logfault::Message &m) noexcept override {
    texts.push_back(m.msg_);
    levels.push_back(m.level_);
  }

  std::vector<std::string> texts;
  std::vector<logfault::LogLevel> levels;
};

}  // namespace

int main() {
  using logfault::LogLevel;
  auto &mgr = logfault::LogManager::Instance();

  auto all = new Capture(LogLevel::DEBUGGING);
  auto warnings = new Capture(LogLevel::WARN);
  mgr.SetHandler(logfault::Handler::ptr_t(all));
  mgr.AddHandler(logfault::Handler::ptr_t(warnings));

  LFLOG_ERROR << "disk " << 7 << " failed";
  LFLOG_INFO << "x=" << 42 << ' ' << 1.5;
  LFLOG_DEBUG << "details";
  LFLOG_TRACE << "below every handler";

  // Exact text, streamed from several operand types.
  check(all->texts.size() == 3, "debug handler gets error, info, debug only");
  if (all->texts.size() == 3) {
    check(all->texts[0] == "disk 7 failed", "error text");
    check(all->texts[1] == "x=42 1.5", "info text");
    check(all->texts[2] == "details", "debug text");
  }

  // Levels reach the handler as logged.
  check(all->levels.size() == 3 && all->levels[0] == LogLevel::ERROR &&
            all->levels[1] == LogLevel::INFO &&
            all->levels[2] == LogLevel::DEBUGGING,
        "message levels");

  // The WARN handler is not given INFO or DEBUG messages.
  check(warnings->texts.size() == 1 && warnings->texts[0] == "disk 7 failed",
        "warn handler gets only the error");

  // The manager's level is the most detailed handler level, so TRACE is
  // dropped before any handler sees it.
  check(mgr.IsRelevant(LogLevel::DEBUGGING) && !mgr.IsRelevant(LogLevel::TRACE),
        "IsRelevant follows the most detailed handler");

  // A lazily evaluated statement below the level never runs its operands.
  int evaluated = 0;
  LFLOG_TRACE << ++evaluated;
  check(evaluated == 0, "filtered statement is not evaluated");

  // Raising the manager's level to filter the debug handler too.
  mgr.SetLevel(LogLevel::NOTICE);
  LFLOG_INFO << "filtered";
  LFLOG_WARN << "kept";
  check(all->texts.size() == 4 && all->texts[3] == "kept", "SetLevel filters");

  // ClearHandlers destroys the handlers and disables logging.
  mgr.ClearHandlers();
  check(!mgr.IsRelevant(LogLevel::ERROR), "no handlers, nothing relevant");
  LFLOG_ERROR << "nobody listens";

  return failures == 0 ? 0 : 1;
}
