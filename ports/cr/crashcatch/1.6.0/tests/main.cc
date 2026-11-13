// crashcatch: the configuration and the handlers initialize() installs, the
// diagnostics block, crash reports written on demand, and the C API.  Nothing
// crashes on purpose, and every file goes under a temporary directory.
#include <CrashCatch.hpp>
// Compiles the C API's implementation here, the way upstream builds its DLL.
#define CRASHCATCH_DLL_EXPORTS
#include <CrashCatchDLL.hpp>

#include <cctype>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <random>
#include <string>

#if !defined(_WIN32)
#include <signal.h>
#include <string.h>
#endif

namespace {

int failures = 0;

void check(bool ok, const char* expr, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, expr);
    ++failures;
  }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

std::string crash_log;
int crash_code = 0;

void on_crash(const char*, const char* log_path, const char*, int code) {
  crash_log = log_path;
  crash_code = code;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
#if defined(NDEBUG)
  const std::string build = "Release";
#else
  const std::string build = "Debug";
#endif
#if defined(_WIN32)
  const std::string platform = "Windows";
#elif defined(__linux__)
  const std::string platform = "Linux";
#else
  const std::string platform = "macOS";
#endif

  const CrashCatch::Config defaults;
  CHECK(defaults.dumpFolder == "./crash_dumps/" &&
        defaults.dumpFileName == "crash");
  CHECK(defaults.enableTextLog && defaults.autoTimestamp &&
        !defaults.showCrashDialog && defaults.includeStackTrace);
  CHECK(defaults.appVersion == "unknown" && defaults.buildConfig == build);
  CHECK(!defaults.onCrash && !defaults.onCrashUpload);

  // A directory of this run's own: create_directory() returns false if the
  // name is taken.
  std::random_device entropy;
  fs::path dir;
  do {
    dir = fs::temp_directory_path() /
          ("cabin-crashcatch-test-" + std::to_string(entropy()));
  } while (!fs::create_directory(dir));

  // The C API fills in the C++ configuration and wraps the C callback.
  const std::string folder = dir.u8string();
  CrashCatch_Config c_config = crashcatch_default_config();
  CHECK(std::string(crashcatch_version()) == "1.5.0");
  CHECK(std::string(c_config.dump_folder) == "./crash_dumps/" &&
        std::string(c_config.dump_file_name) == "crash" &&
        std::string(c_config.app_version) == "unknown" &&
        std::string(c_config.build_config) == "Release");
  CHECK(c_config.enable_text_log == 1 && c_config.auto_timestamp == 1 &&
        c_config.show_crash_dialog == 0 && c_config.include_stack_trace == 1 &&
        c_config.on_crash == nullptr && c_config.on_crash_upload == nullptr);
  c_config.dump_folder = folder.c_str();
  c_config.app_version = "c-api";
  c_config.enable_text_log = 0;
  c_config.on_crash = on_crash;
  CHECK(crashcatch_init(&c_config) == 1 && crashcatch_init(nullptr) == 0);
  CHECK(CrashCatch::globalConfig.dumpFolder == dir &&
        CrashCatch::globalConfig.appVersion == "c-api" &&
        !CrashCatch::globalConfig.enableTextLog &&
        CrashCatch::globalConfig.includeStackTrace &&
        !CrashCatch::globalConfig.onCrashUpload);
  CrashCatch::globalConfig.onCrash({"", dir / "crash.txt", "", 11});
  CHECK(crash_log == (dir / "crash.txt").u8string() && crash_code == 11);

  CrashCatch::Config config;
  config.dumpFolder = dir;
  config.appVersion = "2.4.1";
  config.additionalNotes = "cabin consumer test";
  config.includeStackTrace = false;
  CHECK(CrashCatch::initialize(config));
  CHECK(CrashCatch::globalConfig.dumpFolder == dir &&
        CrashCatch::globalConfig.appVersion == "2.4.1");

  // initialize() installed the crash handlers.
#if defined(_WIN32)
  const LPTOP_LEVEL_EXCEPTION_FILTER filter =
      SetUnhandledExceptionFilter(nullptr);
  CHECK(filter == CrashCatch::UnhandledExceptionHandler);
  SetUnhandledExceptionFilter(filter);
#else
  for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
    struct sigaction action {};
    CHECK(sigaction(sig, nullptr, &action) == 0 &&
          action.sa_handler == CrashCatch::posixSignalHandler);
  }
#endif

  const std::string exe = CrashCatch::getExecutablePath();
  CHECK(fs::is_regular_file(exe));
  const std::string diagnostics = "App Version: 2.4.1\nBuild Config: " + build +
                                  "\nPlatform: " + platform +
                                  "\nExecutable: " + exe +
                                  "\nNotes: cabin consumer test\n";
  CHECK(CrashCatch::getDiagnosticsInfo() == diagnostics);

  // A report written on demand, as the handlers write one; writeCrashLog
  // creates the missing reports directory.
  CrashCatch::writeCrashLog(dir / "reports" / "first.txt",
                            "2026-01-02_03-04-05", SIGSEGV);
#if defined(_WIN32)
  const std::string signal_line;
#elif defined(__linux__)
  const std::string signal_line = std::string("Signal: ") + strsignal(SIGSEGV) +
                                  " (" + std::to_string(SIGSEGV) + ")\n";
#else
  const std::string signal_line =
      std::string("Signal: ") + strsignal(SIGSEGV) + "\n";
#endif
  CHECK(read_file(dir / "reports" / "first.txt") ==
        "Crash Report\n============\n" + signal_line +
            "Timestamp: 2026-01-02_03-04-05\n\nEnvironment Info:\n" +
            diagnostics + "\n");

  // Without a timestamp the report says N/A; with includeStackTrace it ends
  // with the stack, frame 0 first.
  CrashCatch::globalConfig.includeStackTrace = true;
  CrashCatch::writeCrashLog(dir / "reports" / "second.txt", "", SIGABRT);
  const std::string second = read_file(dir / "reports" / "second.txt");
  CHECK(second.find("Timestamp: N/A\n\n") != std::string::npos);
  CHECK(second.find(diagnostics + "\n\nStack Trace:\n  [0]: ") !=
        std::string::npos);
  CHECK(second.find("\n  [1]: ") != std::string::npos);

  // Without notes the diagnostics have no Notes line.
  CrashCatch::globalConfig.additionalNotes.clear();
  CHECK(CrashCatch::getDiagnosticsInfo() ==
        diagnostics.substr(0, diagnostics.find("Notes: ")));

  // enable() is initialize() with the defaults.
  CHECK(CrashCatch::enable() &&
        CrashCatch::globalConfig.appVersion == "unknown" &&
        CrashCatch::globalConfig.dumpFolder == "./crash_dumps/");

  const std::string stamp = CrashCatch::getTimestamp();
  const std::string shape = "0000-00-00_00-00-00";
  bool shaped = stamp.size() == shape.size();
  for (std::size_t i = 0; shaped && i < stamp.size(); ++i) {
    shaped = shape[i] == '0'
                 ? std::isdigit(static_cast<unsigned char>(stamp[i])) != 0
                 : stamp[i] == shape[i];
  }
  CHECK(shaped);

  // The directory, reports/ and the two reports.
  CHECK(fs::remove_all(dir) == 4);

  if (failures != 0) {
    return 1;
  }
  std::puts("crashcatch ok");
  return 0;
}
