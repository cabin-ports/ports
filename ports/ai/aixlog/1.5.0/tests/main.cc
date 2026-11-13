// aixlog: LOG lines reach a callback sink and a stream sink, each filtering by
// severity and tag; the stream sink's format gives exact text.
#include <aixlog.hpp>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

struct Record {
  AixLog::Severity severity;
  std::string tag;
  std::string function;
  std::string message;
};

std::vector<Record> records;

void record(const AixLog::Metadata& metadata, const std::string& message) {
  records.push_back(
      {metadata.severity, metadata.tag.text, metadata.function.name, message});
}

bool is(const Record& r, AixLog::Severity severity, const char* tag,
        const char* message) {
  return r.severity == severity && r.tag == tag && r.function == "main" &&
         r.message == message;
}

}  // namespace

int main() {
  // The logger takes over std::clog: see the limitations in cabin.toml.
  std::streambuf* clog_buffer = std::clog.rdbuf();

  CHECK(AixLog::to_severity("WARNING") == AixLog::Severity::warning);
  CHECK(AixLog::to_severity("bogus", AixLog::Severity::error) ==
        AixLog::Severity::error);
  CHECK(AixLog::to_string(AixLog::Severity::warning) == "Warn");

  // The callback sink takes tag "net" from debug up and other tags from
  // warning up; the stream sink takes everything from info up.
  AixLog::Filter callback_filter;
  callback_filter.add_filter("net:debug");
  callback_filter.add_filter("*:warning");
  auto callback =
      std::make_shared<AixLog::SinkCallback>(callback_filter, record);
  std::ostringstream out;
  std::streambuf* cout_buffer = std::cout.rdbuf(out.rdbuf());
  auto stream = std::make_shared<AixLog::SinkCout>(
      AixLog::Severity::info, "[#severity] (#tag_func) #message");
  AixLog::Log::init({callback, stream});

  LOG(DEBUG, "net") << "handshake\n";
  LOG(TRACE, "net") << "bytes\n";
  LOG(INFO) << "started\n";
  LOG(NOTICE, "net") << "connected\n";
  LOG(WARNING) << "disk low\n";
  // Not LOG(ERROR): see the Windows limitation in cabin.toml.
  LOG(AixLog::Severity::error, "db") << "query failed\n";
  LOG(FATAL, "db") << "giving up\n";

  CHECK(records.size() == 5);
  if (records.size() == 5) {
    CHECK(is(records[0], AixLog::Severity::debug, "net", "handshake"));
    CHECK(is(records[1], AixLog::Severity::notice, "net", "connected"));
    CHECK(is(records[2], AixLog::Severity::warning, "", "disk low"));
    CHECK(is(records[3], AixLog::Severity::error, "db", "query failed"));
    CHECK(is(records[4], AixLog::Severity::fatal, "db", "giving up"));
  }
  // #tag_func is the tag, or the function when there is none.
  CHECK(out.str() ==
        "[Info] (main) started\n"
        "[Notice] (net) connected\n"
        "[Warn] (main) disk low\n"
        "[Error] (db) query failed\n"
        "[Fatal] (db) giving up\n");

  // With a fixed timestamp, #ms is its millisecond part.
  out.str("");
  AixLog::Log::init<AixLog::SinkCout>(AixLog::Severity::trace,
                                      "#ms #severity #message");
  std::clog << AixLog::Severity::info
            << AixLog::Timestamp(std::chrono::system_clock::time_point(
                   std::chrono::milliseconds(1234)))
            << "tick\n";
  CHECK(out.str() == "234 Info tick\n");

  // SinkNative wraps the platform's system-log sink, the code that needs the
  // Windows and Android link-libs; it is never attached, so it writes nothing.
  AixLog::SinkNative native("aixlog-test", AixLog::Severity::fatal);
  CHECK(native.get_logger() != nullptr);

#ifdef _WIN32
  // wingdi.h's `#define ERROR 0` survives aixlog.hpp: see cabin.toml.
  records.clear();
  AixLog::Log::init<AixLog::SinkCallback>(AixLog::Severity::trace, record);
  LOG(ERROR) << "macro\n";
  CHECK(records.size() == 1 && records[0].severity == AixLog::Severity::trace);
#endif

  std::cout.rdbuf(cout_buffer);
  std::clog.rdbuf(clog_buffer);
  if (failures != 0) {
    return 1;
  }
  std::printf("aixlog ok\n");
  return 0;
}
