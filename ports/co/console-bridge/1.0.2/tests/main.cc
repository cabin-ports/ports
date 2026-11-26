// C++ consumer: routes log messages to a custom handler and checks the level
// filter, formatting, file and line, handler switching, and OutputHandlerFile.
#include <console_bridge/console.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace console_bridge;

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "FAIL line " << __LINE__ << ": " #cond << std::endl;  \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

struct Message {
  std::string text;
  LogLevel level;
  std::string file;
  int line;
};

struct Capture : OutputHandler {
  std::vector<Message> messages;
  void log(const std::string &text, LogLevel level, const char *filename,
           int line) override {
    messages.push_back({text, level, filename, line});
  }
};

}  // namespace

int main() {
  // The default level is WARN and the default handler is the console one.
  CHECK(getLogLevel() == CONSOLE_BRIDGE_LOG_WARN);
  CHECK(getOutputHandler() != nullptr);
  CHECK(dynamic_cast<OutputHandlerSTD *>(getOutputHandler()) != nullptr);

  Capture cap;
  useOutputHandler(&cap);
  CHECK(getOutputHandler() == &cap);

  // WARN level: error and warning pass, inform and debug are dropped.
  const int err_line = __LINE__ + 1;
  CONSOLE_BRIDGE_logError("one integer: %d", 42);
  const int warn_line = __LINE__ + 1;
  CONSOLE_BRIDGE_logWarn("%s has %d and %.2f", "text", -7, 1 / 3.0);
  CONSOLE_BRIDGE_logInform("hidden");
  CONSOLE_BRIDGE_logDebug("hidden");
  CHECK(cap.messages.size() == 2);
  if (cap.messages.size() == 2) {
    CHECK(cap.messages[0].text == "one integer: 42");
    CHECK(cap.messages[0].level == CONSOLE_BRIDGE_LOG_ERROR);
    CHECK(cap.messages[0].file == __FILE__);
    CHECK(cap.messages[0].line == err_line);
    CHECK(cap.messages[1].text == "text has -7 and 0.33");
    CHECK(cap.messages[1].level == CONSOLE_BRIDGE_LOG_WARN);
    CHECK(cap.messages[1].line == warn_line);
  }

  // DEBUG level lets all four through, in order, with their own levels.
  cap.messages.clear();
  setLogLevel(CONSOLE_BRIDGE_LOG_DEBUG);
  CHECK(getLogLevel() == CONSOLE_BRIDGE_LOG_DEBUG);
  CONSOLE_BRIDGE_logDebug("d");
  CONSOLE_BRIDGE_logInform("i");
  CONSOLE_BRIDGE_logWarn("w");
  CONSOLE_BRIDGE_logError("e");
  CHECK(cap.messages.size() == 4);
  if (cap.messages.size() == 4) {
    CHECK(cap.messages[0].level == CONSOLE_BRIDGE_LOG_DEBUG);
    CHECK(cap.messages[1].level == CONSOLE_BRIDGE_LOG_INFO);
    CHECK(cap.messages[2].level == CONSOLE_BRIDGE_LOG_WARN);
    CHECK(cap.messages[3].level == CONSOLE_BRIDGE_LOG_ERROR);
    CHECK(cap.messages[3].text == "e");
  }

  // NONE silences everything; ERROR keeps only errors.
  cap.messages.clear();
  setLogLevel(CONSOLE_BRIDGE_LOG_NONE);
  CONSOLE_BRIDGE_logError("silent");
  CHECK(cap.messages.empty());
  setLogLevel(CONSOLE_BRIDGE_LOG_ERROR);
  CONSOLE_BRIDGE_logWarn("silent");
  CONSOLE_BRIDGE_logError("kept");
  CHECK(cap.messages.size() == 1 && cap.messages[0].text == "kept");

  // A message longer than the 1024-byte buffer is truncated to 1023 bytes.
  cap.messages.clear();
  setLogLevel(CONSOLE_BRIDGE_LOG_WARN);
  CHECK(getLogLevel() == CONSOLE_BRIDGE_LOG_WARN);
  CONSOLE_BRIDGE_logError("%s", std::string(2000, 'x').c_str());
  CHECK(cap.messages.size() == 1 &&
        cap.messages[0].text == std::string(1023, 'x'));

  // noOutputHandler drops messages; restorePreviousOutputHandler brings the
  // handler back.
  cap.messages.clear();
  noOutputHandler();
  CHECK(getOutputHandler() == nullptr);
  CONSOLE_BRIDGE_logError("dropped");
  CHECK(cap.messages.empty());
  restorePreviousOutputHandler();
  CHECK(getOutputHandler() == &cap);
  CONSOLE_BRIDGE_logError("back");
  CHECK(cap.messages.size() == 1 && cap.messages[0].text == "back");

  // Switching handlers moves messages to the new one; restoring returns.
  Capture other;
  useOutputHandler(&other);
  CONSOLE_BRIDGE_logError("to other");
  restorePreviousOutputHandler();
  CONSOLE_BRIDGE_logError("to cap");
  CHECK(other.messages.size() == 1 && other.messages[0].text == "to other");
  CHECK(cap.messages.size() == 2 && cap.messages[1].text == "to cap");

  // OutputHandlerFile appends "<Level>   <text>" and, from WARN up, the
  // location line.
  const char *dir = std::getenv("TMPDIR");
  if (!dir) dir = std::getenv("TEMP");
  const std::string path_str =
      std::string(dir ? dir : "/tmp") + "/console_bridge_test.log";
  const char *path = path_str.c_str();
  std::remove(path);
  const int file_line = __LINE__ + 3;
  {
    OutputHandlerFile file(path);
    file.log("boom", CONSOLE_BRIDGE_LOG_ERROR, __FILE__, file_line);
    file.log("note", CONSOLE_BRIDGE_LOG_INFO, __FILE__, file_line);
  }
  std::ifstream in(path);
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  in.close();
  std::remove(path);
  CHECK(content == "Error:   boom\n         at line " +
                       std::to_string(file_line) + " in " + __FILE__ +
                       "\nInfo:    note\n");

  noOutputHandler();  // Leave no dangling pointer to the locals above.
  if (failures == 0) std::cout << "ok" << std::endl;
  return failures == 0 ? 0 : 1;
}
