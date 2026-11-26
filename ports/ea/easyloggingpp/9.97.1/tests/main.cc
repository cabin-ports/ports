#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "easylogging++.h"

INITIALIZE_EASYLOGGINGPP

static int failures = 0;
#define CHECK_TRUE(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
  CHECK_TRUE(el::VersionInfo::version() == "9.97.1");

  const char* tmp = std::getenv("TMPDIR");
  const std::string path = std::string(tmp ? tmp : ".") + "/cabin-easyloggingpp-test.log";
  std::remove(path.c_str());
  el::Logger* logger = el::Loggers::getLogger("cabin");
  el::Configurations conf;
  conf.setToDefault();
  conf.setGlobally(el::ConfigurationType::Format, "%level %msg");
  conf.setGlobally(el::ConfigurationType::Filename, path);
  // setGlobally skips the Global level, which keeps the default myeasylog.log.
  conf.set(el::Level::Global, el::ConfigurationType::Filename, path);
  conf.setGlobally(el::ConfigurationType::ToFile, "true");
  conf.setGlobally(el::ConfigurationType::ToStandardOutput, "false");
  conf.set(el::Level::Debug, el::ConfigurationType::Enabled, "false");
  el::Loggers::reconfigureLogger(logger, conf);

  CHECK_TRUE(logger != nullptr);
  CHECK_TRUE(logger == el::Loggers::getLogger("cabin"));
  CHECK_TRUE(logger->id() == "cabin");

  CLOG(INFO, "cabin") << "hello " << 42;
  CLOG(WARNING, "cabin") << "careful";
  CLOG(DEBUG, "cabin") << "hidden";
  CLOG(ERROR, "cabin") << "failed";
  el::Loggers::flushAll();

  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  CHECK_TRUE(ss.str() == "INFO hello 42\nWARNING careful\nERROR failed\n");
  if (failures) std::fprintf(stderr, "got: %s\n", ss.str().c_str());

  el::Loggers::unregisterLogger("cabin");
  in.close();
  std::remove(path.c_str());
  return failures == 0 ? 0 : 1;
}
