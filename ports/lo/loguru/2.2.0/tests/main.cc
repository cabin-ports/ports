#include <loguru.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

typedef std::vector<std::pair<int, std::string>> Captured;

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::cerr << "FAILED: " << what << "\n";
    ++failures;
  }
}

static void capture(void* user_data, const loguru::Message& message) {
  static_cast<Captured*>(user_data)->push_back(
      std::make_pair(static_cast<int>(message.verbosity), message.message));
}

static void throwing_handler(const loguru::Message& message) {
  throw std::runtime_error(std::string(message.prefix) + message.message);
}

int main() {
  loguru::g_stderr_verbosity = loguru::Verbosity_OFF;

  Captured captured;

  // The probe's working directory may be read-only; the system temp directory is not.
  const char* tmp = std::getenv("TMPDIR");
  if (!tmp) tmp = std::getenv("TEMP");
  if (!tmp) tmp = ".";
  const std::string path_str = std::string(tmp) + "/loguru_consumer_test.log";
  const char* path = path_str.c_str();
  expect(loguru::add_file(path, loguru::Truncate, loguru::Verbosity_WARNING),
         "add_file");
  // Registered after add_file, which itself logs a "Logging to" line at INFO.
  loguru::add_callback("test", capture, &captured, loguru::Verbosity_INFO);

  LOG_F(INFO, "The magic number is %d", 42);
  LOG_F(WARNING, "w%s", "x");
  LOG_F(ERROR, "e%c", '!');
  VLOG_F(1, "hidden");
  loguru::flush();

  expect(captured.size() == 3, "three messages captured");
  if (captured.size() == 3) {
    expect(captured[0].first == 0 && captured[0].second == "The magic number is 42",
           "INFO message");
    expect(captured[1].first == -1 && captured[1].second == "wx", "WARNING message");
    expect(captured[2].first == -2 && captured[2].second == "e!", "ERROR message");
  }

  // The file sink takes WARNING and above only: two lines, each ending in its message.
  std::ifstream in(path);
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    if (line.find("main.cc:") != std::string::npos) lines.push_back(line);
  }
  expect(lines.size() == 2, "two file lines");
  if (lines.size() == 2) {
    expect(lines[0].size() >= 2 && lines[0].compare(lines[0].size() - 2, 2, "wx") == 0,
           "file WARNING line");
    expect(lines[1].size() >= 2 && lines[1].compare(lines[1].size() - 2, 2, "e!") == 0,
           "file ERROR line");
  }
  expect(loguru::remove_callback("test"), "remove_callback");
  loguru::shutdown();
  in.close();
  std::remove(path);

  expect(std::string(loguru::textprintf("%d-%s", 7, "x").c_str()) == "7-x", "textprintf");
  expect(std::string(loguru::format_value(42).c_str()) == "42", "format_value int");
  expect(std::string(loguru::format_value('c').c_str()) == "c", "format_value char");
  expect(loguru::get_verbosity_from_name("ERROR") == loguru::Verbosity_ERROR,
         "get_verbosity_from_name");

  CHECK_EQ_F(2 + 2, 4);
  loguru::set_fatal_handler(throwing_handler);
  std::string what;
  try {
    CHECK_F(1 + 1 == 3, "boom %d", 1);
  } catch (const std::runtime_error& e) {
    what = e.what();
  }
  expect(what == "CHECK FAILED:  1 + 1 == 3  boom 1", "CHECK_F fatal handler");
  what.clear();
  try {
    CHECK_LT_F(5, 3);
  } catch (const std::runtime_error& e) {
    what = e.what();
  }
  expect(what.find("CHECK FAILED:  5 < 3") == 0, "CHECK_LT_F fatal handler");

  return failures == 0 ? 0 : 1;
}
