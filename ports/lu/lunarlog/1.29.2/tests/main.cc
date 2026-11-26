#include <lunar_log.hpp>

#include <cstdio>
#include <string>
#include <vector>

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      return 1;                                                          \
    }                                                                    \
  } while (0)

int main() {
  std::vector<minta::LogEntry> seen;
  auto logger =
      minta::LunarLog::configure()
          .minLevel(minta::LogLevel::WARN)
          .writeTo<minta::CallbackSink>(minta::CallbackSink::EntryCallback(
              [&seen](const minta::LogEntry& e) {
                minta::LogEntry c;
                c.level = e.level;
                c.message = e.message;
                c.templateStr = e.templateStr;
                c.properties = e.properties;
                seen.push_back(std::move(c));
              }))
          .build();

  // Below the minimum level: dropped.
  logger.info("User {name} logged in", "name", "alice");
  logger.flush();
  CHECK(seen.empty());

  logger.error("User {name} failed {count} times", "name", "alice", "count", 3);
  logger.warn("plain warning");
  logger.flush();
  CHECK(seen.size() == 2);
  CHECK(seen[0].level == minta::LogLevel::ERROR);
  CHECK(seen[0].message == "User alice failed 3 times");
  CHECK(seen[0].templateStr == "User {name} failed {count} times");
  CHECK(seen[0].properties.size() == 2);
  CHECK(seen[0].properties[0].name == "name");
  CHECK(seen[0].properties[0].value == "alice");
  CHECK(seen[0].properties[1].name == "count");
  CHECK(seen[0].properties[1].value == "3");
  CHECK(seen[1].level == minta::LogLevel::WARN);
  CHECK(seen[1].message == "plain warning");

  // Lowering the minimum level at run time lets INFO through.
  logger.setMinLevel(minta::LogLevel::INFO);
  logger.info("User {name} logged in", "name", "bob");
  logger.flush();
  CHECK(seen.size() == 3);
  CHECK(seen[2].message == "User bob logged in");
  return 0;
}
