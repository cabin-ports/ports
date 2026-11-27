// A C++20 consumer: bq_log.h declares the fast_info<"..."> form only there.
#include <bq_log/bq_log.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<std::string> fetched;

void BQ_STDCALL on_fetch(uint64_t, int32_t, bq::log_level level,
                         const char* content, int32_t length) {
  if (level == bq::log_level::info) {
    fetched.emplace_back(content, static_cast<size_t>(length));
  }
}

}  // namespace

int main() {
  bq::log::set_console_buffer_enable(true);
  bq::log log = bq::log::create_log("cabin_cxx20",
                                    "appenders_config.con.type=console\n"
                                    "appenders_config.con.levels=[all]\n");
  if (!log.is_valid()) {
    std::fprintf(stderr, "create_log failed\n");
    return 1;
  }
  log.fast_info<"template:{} {}">(20, "ok");
  log.force_flush();
  while (bq::log::fetch_and_remove_console_buffer(on_fetch)) {
  }
  const std::string tail = "template:20 ok";
  if (fetched.size() != 1 || fetched[0].size() < tail.size() ||
      fetched[0].compare(fetched[0].size() - tail.size(), tail.size(),
                         tail) != 0) {
    std::fprintf(stderr, "fast_info<...> entry missing or wrong\n");
    return 1;
  }
  return 0;
}
