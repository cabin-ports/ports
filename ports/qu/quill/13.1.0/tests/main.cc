#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/bundled/fmt/format.h>
#include <quill/sinks/Sink.h>
#include <quill/std/Vector.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Keeps each formatted statement without its trailing newline.
class CaptureSink final : public quill::Sink {
 public:
  void write_log(quill::MacroMetadata const*, uint64_t, std::string_view,
                 std::string_view, std::string const&, std::string_view,
                 quill::LogLevel, std::string_view, std::string_view,
                 std::vector<std::pair<std::string, std::string>> const*,
                 std::string_view, std::string_view log_statement) override {
    std::lock_guard<std::mutex> lock(mutex_);
    statements_.emplace_back(log_statement.substr(0, log_statement.size() - 1));
  }

  void flush_sink() noexcept override {}

  std::vector<std::string> Take() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> taken;
    taken.swap(statements_);
    return taken;
  }

 private:
  std::mutex mutex_;
  std::vector<std::string> statements_;
};

void TestFormatting(quill::Logger* logger, CaptureSink& capture) {
  // A new logger takes the QUILL_LOG_LEVEL environment variable when set.
  logger->set_log_level(quill::LogLevel::Info);
  std::string text = "original";
  LOG_INFO(logger, "Hello {} {}", "quill", 13);
  LOG_INFO(logger, "no arguments");
  LOG_WARNING(logger, "{:>5}|{:.2f}|{:#x}", 42, 3.14159, 255);
  LOG_DEBUG(logger, "hidden {}", 1);  // below Info
  LOG_INFO(logger, "text={}", text);
  text = "changed";  // arguments are captured at the call
  std::vector<int> values{1, 2, 3};
  LOG_ERROR(logger, "values={}", values);
  logger->set_log_level(quill::LogLevel::Debug);
  LOG_DEBUG(logger, "shown {}", true);
  logger->flush_log();

  std::vector<std::string> const expected{
      "INFO app Hello quill 13",     "INFO app no arguments",
      "WARNING app    42|3.14|0xff", "INFO app text=original",
      "ERROR app values=[1, 2, 3]",  "DEBUG app shown true",
  };
  std::vector<std::string> const got = capture.Take();
  check(got == expected, "formatted statements");
  if (got != expected) {
    for (std::string const& line : got) {
      std::fprintf(stderr, "  got: %s\n", line.c_str());
    }
  }
}

void TestThreads(quill::Logger* logger, CaptureSink& capture) {
  constexpr int kThreads = 4;
  constexpr int kPerThread = 250;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([logger, t] {
      for (int i = 0; i < kPerThread; ++i) {
        LOG_INFO(logger, "{} {}", t, i);
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  // flush_log() covers statements up to its own timestamp; stopping the
  // backend drains every thread's queue, as upstream's threaded tests do.
  quill::Backend::stop();

  std::vector<std::string> const got = capture.Take();
  check(got.size() == kThreads * kPerThread, "every threaded statement");
  std::vector<int> next(kThreads, 0);
  bool ordered = true;
  for (std::string const& line : got) {
    int t = -1;
    int i = -1;
    if (std::sscanf(line.c_str(), "INFO app %d %d", &t, &i) != 2 || t < 0 ||
        t >= kThreads || i != next[t]) {
      ordered = false;
      break;
    }
    ++next[t];
  }
  check(ordered, "each thread's statements arrive in order");
}

}  // namespace

int main() {
  check(fmtquill::format("{:08.3f}|{:<4}|", 3.14159, "ab") == "0003.142|ab  |",
        "bundled fmtquill");

  quill::Backend::start();
  auto sink = quill::Frontend::create_or_get_sink<CaptureSink>("capture");
  auto& capture = static_cast<CaptureSink&>(*sink);
  quill::PatternFormatterOptions const pattern{
      "%(log_level) %(logger) %(message)"};
  quill::Logger* logger =
      quill::Frontend::create_or_get_logger("app", sink, pattern);

  TestFormatting(logger, capture);
  TestThreads(logger, capture);  // stops the backend

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("quill consumer test passed");
  return 0;
}
