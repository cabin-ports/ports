#include <bq_log/bq_log.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct entry {
  bq::log_level level;
  std::string text;
};

std::vector<entry> fetched;

void BQ_STDCALL on_fetch(uint64_t, int32_t, bq::log_level level,
                         const char* content, int32_t length) {
  fetched.push_back({level, std::string(content, static_cast<size_t>(length))});
}

bool ends_with(const std::string& s, const std::string& tail) {
  return s.size() >= tail.size() &&
         s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

int fail(const char* what) {
  std::fprintf(stderr, "FAIL: %s\n", what);
  for (const entry& e : fetched) {
    std::fprintf(stderr, "  [%d] %s\n", static_cast<int>(e.level),
                 e.text.c_str());
  }
  return 1;
}

}  // namespace

int main() {
  if (std::strcmp(bq::log::get_version().c_str(), "2.8.3") != 0) {
    return fail("get_version");
  }

  // The console appender keeps info and error; the snapshot keeps error.
  bq::log::set_console_buffer_enable(true);
  bq::log log = bq::log::create_log("cabin_cc",
                                    "appenders_config.con.type=console\n"
                                    "appenders_config.con.levels=[info,error]\n"
                                    "snapshot.buffer_size=65536\n"
                                    "snapshot.levels=[error]\n");
  if (!log.is_valid()) {
    return fail("create_log");
  }
  if (bq::log::get_log_by_name("cabin_cc").get_id() != log.get_id()) {
    return fail("get_log_by_name");
  }

  log.debug("dropped {}", 1);
  log.info("value:{} name:{}", 42, "abc");
  log.error("neg:{} flag:{} unit:{}", -7, true, 'x');
  log.force_flush();
  while (bq::log::fetch_and_remove_console_buffer(on_fetch)) {
  }

  if (fetched.size() != 2) {
    return fail("console entries");
  }
  if (fetched[0].level != bq::log_level::info ||
      !ends_with(fetched[0].text, "value:42 name:abc")) {
    return fail("info entry");
  }
  if (fetched[1].level != bq::log_level::error ||
      !ends_with(fetched[1].text, "neg:-7 flag:TRUE unit:x")) {
    return fail("error entry");
  }

  const std::string snapshot = log.take_snapshot("gmt").c_str();
  if (!ends_with(snapshot, "neg:-7 flag:TRUE unit:x\n") ||
      snapshot.find("value:42") != std::string::npos) {
    std::fprintf(stderr, "snapshot: %s\n", snapshot.c_str());
    return fail("take_snapshot");
  }

  fetched.clear();
  BQ_LOG_FAST_INFO(log, "fast:{} {}", 7, "ok");
  log.force_flush();
  while (bq::log::fetch_and_remove_console_buffer(on_fetch)) {
  }
  if (fetched.size() != 1 || fetched[0].level != bq::log_level::info ||
      !ends_with(fetched[0].text, "fast:7 ok")) {
    return fail("BQ_LOG_FAST_INFO entry");
  }
  return 0;
}
