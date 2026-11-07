// Exercises compiled code from each of Abseil's main components and checks
// that patch 0001's options are in effect.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/options.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/crc/crc32c.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/numeric/int128.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/random/seed_sequences.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"

ABSL_FLAG(int, cabin_port_depth, 3, "a flag the test reads, sets and parses");

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

absl::StatusOr<int> ParsePositive(absl::string_view text) {
  int value = 0;
  if (!absl::SimpleAtoi(text, &value)) return absl::InvalidArgumentError("not a number");
  if (value <= 0) return absl::OutOfRangeError(absl::StrCat(value, " is not positive"));
  return value;
}

class CaptureSink : public absl::LogSink {
 public:
  void Send(const absl::LogEntry& entry) override {
    if (entry.log_severity() == absl::LogSeverity::kWarning) {
      text = std::string(entry.text_message());
    }
  }
  std::string text;
};

}  // namespace

int main() {
  // Strings.
  check(absl::StrCat("a", 1, "-", 2.5, absl::Hex(255), absl::Dec(7, absl::kZeroPad3)) ==
            "a1-2.5ff007",
        "StrCat");
  std::vector<std::string> parts = absl::StrSplit("x,y,,z", ',', absl::SkipEmpty());
  check(parts == std::vector<std::string>{"x", "y", "z"}, "StrSplit");
  std::pair<std::string, std::string> kv = absl::StrSplit("k=v=w", absl::MaxSplits('=', 1));
  check(kv.first == "k" && kv.second == "v=w", "StrSplit into a pair with MaxSplits");
  check(absl::StrJoin(parts, "+") == "x+y+z", "StrJoin");
  check(absl::StrFormat("%05.1f|%-4s|%d|%x", 3.14159, "ab", -7, 255u) == "003.1|ab  |-7|ff",
        "StrFormat");
  check(absl::StrReplaceAll("a-b-c", {{"-", "::"}}) == "a::b::c", "StrReplaceAll");
  int64_t parsed = 0;
  double real = 0;
  check(absl::SimpleAtoi(" -9223372036854775808", &parsed) && parsed == INT64_MIN &&
            !absl::SimpleAtoi("12x", &parsed) && absl::SimpleAtod("2.5e3", &real) && real == 2500,
        "number parsing");
  check(absl::Base64Escape("hello") == "aGVsbG8=" &&
            absl::WebSafeBase64Escape("\xfb\xff") == "-_8" &&
            absl::CEscape("a\n\"") == "a\\n\\\"",
        "escaping");
  std::string unescaped;
  check(absl::Base64Unescape("aGVsbG8=", &unescaped) && unescaped == "hello", "Base64Unescape");

  // Containers and hashing.
  absl::flat_hash_map<std::string, int> map;
  for (int i = 0; i < 1000; ++i) map[absl::StrCat("key", i)] = i;
  map.erase("key500");
  check(map.size() == 999 && map.at("key999") == 999 && !map.contains("key500") &&
            map.find("key0")->second == 0,
        "flat_hash_map");
  absl::btree_map<int, std::string> ordered = {{3, "c"}, {1, "a"}, {2, "b"}};
  std::string order;
  for (const auto& [key, value] : ordered) order += value;
  check(order == "abc" && ordered.lower_bound(2)->second == "b", "btree_map order");
  absl::InlinedVector<int, 4> small = {1, 2, 3, 4};
  small.push_back(5);
  check(small.size() == 5 && small.back() == 5, "InlinedVector past its inline capacity");
  // Strings over 32 bytes hash through the compiled code.
  const std::string long_a(40, 'h');
  const std::string long_b = std::string(39, 'h') + "i";
  check(absl::HashOf(long_a, 7) == absl::HashOf(std::string(40, 'h'), 7) &&
            absl::HashOf(long_a) != absl::HashOf(long_b) &&
            absl::Hash<std::pair<int, std::string>>{}({1, "x"}) ==
                absl::Hash<std::pair<int, std::string>>{}({1, "x"}),
        "absl::Hash");

  // uint128 arithmetic, CRC32C and Cord.
  const absl::uint128 big = absl::MakeUint128(0x0123456789abcdefu, 0xfedcba9876543210u);
  check((big * 100 + 99) / 100 == big && (big * 100 + 99) % 100 == 99 &&
            absl::Uint128Max() / big == 0xe1u,
        "uint128 division");
  check(absl::StrCat(big) == "1512366075204170947332355369683137040" &&
            absl::StrCat(absl::Uint128Max()) == "340282366920938463463374607431768211455",
        "uint128 to decimal");
  check(absl::ComputeCrc32c("123456789") == absl::crc32c_t{0xE3069283}, "CRC32C check value");
  // Inputs over 64 bytes skip the inline path and use the compiled engine.
  const std::string as(1000, 'a');
  const absl::crc32c_t crc_as = absl::ComputeCrc32c(as);
  check(crc_as == absl::crc32c_t{0x9F19EF6A} &&
            absl::ConcatCrc32c(absl::ComputeCrc32c(as.substr(0, 600)),
                               absl::ComputeCrc32c(as.substr(600)), 400) == crc_as,
        "CRC32C of a long buffer");
  absl::Cord cord("hello");
  cord.Append(", ");
  cord.Append(std::string(100, 'x'));
  cord.Prepend(">");
  check(cord.size() == 108 && cord.StartsWith(">hello, x") &&
            std::string(cord).substr(100) == "xxxxxxxx",
        "Cord");

  // Time.
  const absl::TimeZone utc = absl::UTCTimeZone();
  const absl::Time t = absl::FromCivil(absl::CivilSecond(2026, 8, 17, 12, 34, 56), utc);
  check(absl::FormatTime("%Y-%m-%dT%H:%M:%S%Ez", t, utc) == "2026-08-17T12:34:56+00:00",
        "FormatTime in UTC");
  check(absl::FormatTime("%H:%M %a", t, absl::FixedTimeZone(9 * 3600)) == "21:34 Mon",
        "FormatTime in a fixed zone");
  absl::Time parsed_time;
  std::string error;
  check(absl::ParseTime(absl::RFC3339_full, "2026-08-17T12:34:56.5+02:00", &parsed_time, &error) &&
            parsed_time - t == absl::Milliseconds(500) - absl::Hours(2),
        "ParseTime with an offset");
  check(absl::FormatDuration(absl::Hours(1) + absl::Minutes(30)) == "1h30m" &&
            absl::ToInt64Seconds(absl::Hours(2)) == 7200,
        "durations");
  check(absl::CivilDay(2026, 2, 28) + 1 == absl::CivilDay(2026, 3, 1) &&
            absl::GetWeekday(absl::CivilDay(2026, 8, 17)) == absl::Weekday::monday &&
            absl::GetYearDay(absl::CivilDay(2026, 12, 31)) == 365,
        "civil time");

  // Status and StatusOr.
  absl::Status missing = absl::NotFoundError("no key");
  check(absl::IsNotFound(missing) && missing.ToString() == "NOT_FOUND: no key" &&
            absl::OkStatus().ok(),
        "Status");
  missing.SetPayload("type.example/detail", absl::Cord("payload"));
  check(missing.GetPayload("type.example/detail").value_or(absl::Cord()) == "payload",
        "Status payload");
  absl::StatusOr<int> good = ParsePositive("42");
  absl::StatusOr<int> zero = ParsePositive("0");
  absl::StatusOr<int> junk = ParsePositive("x");
  check(good.ok() && *good == 42 && zero.status().code() == absl::StatusCode::kOutOfRange &&
            zero.status().message() == "0 is not positive" &&
            absl::IsInvalidArgument(junk.status()),
        "StatusOr");

  // Synchronization: four threads add under a Mutex while a fifth waits
  // for the total on a Condition, then signals a Notification.
  absl::Mutex mu;
  int counter = 0;
  absl::Notification done;
  std::thread waiter([&] {
    mu.LockWhen(absl::Condition(+[](int* c) { return *c == 40000; }, &counter));
    mu.unlock();
    done.Notify();
  });
  std::vector<std::thread> workers;
  for (int w = 0; w < 4; ++w) {
    workers.emplace_back([&] {
      for (int i = 0; i < 10000; ++i) {
        absl::MutexLock lock(mu);
        ++counter;
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (!done.WaitForNotificationWithTimeout(absl::Seconds(30))) {
    // The waiter may still be blocked, so leave without joining it.
    std::fprintf(stderr, "FAIL: Condition wakes the waiter\n");
    std::printf("abseil (C++): FAILED\n");
    std::fflush(stdout);
    std::_Exit(1);
  }
  waiter.join();
  {
    absl::MutexLock lock(mu);
    check(counter == 40000, "Mutex serializes the increments");
  }

  // Logging: a LOG statement reaches a registered sink.
  CaptureSink sink;
  absl::AddLogSink(&sink);
  LOG(WARNING) << "depth " << 3;
  absl::RemoveLogSink(&sink);
  check(sink.text == "depth 3", "LOG reaches a LogSink");

  // A flag: read, set, then parsed through the registry.
  check(absl::GetFlag(FLAGS_cabin_port_depth) == 3, "flag default");
  absl::SetFlag(&FLAGS_cabin_port_depth, 7);
  absl::CommandLineFlag* flag = absl::FindCommandLineFlag("cabin_port_depth");
  check(flag != nullptr && flag->CurrentValue() == "7", "flag registry");
  std::string flag_error;
  check(flag != nullptr && flag->ParseFrom("11", &flag_error) &&
            absl::GetFlag(FLAGS_cabin_port_depth) == 11 && !flag->ParseFrom("eleven", &flag_error),
        "flag parsing");

  // Random: equal seeds give equal BitGen streams within a process.  The
  // distributions draw from a fixed-seed std::mt19937 instead, because a
  // BitGen's stream may differ between processes (absl/random/random.h).
  std::seed_seq seed_a{1, 2, 3};
  std::seed_seq seed_b{1, 2, 3};
  absl::BitGen gen_a(seed_a);
  absl::BitGen gen_b(seed_b);
  bool same = true;
  for (int i = 0; i < 16; ++i) same = same && gen_a() == gen_b();
  check(same, "equal seeds give equal BitGen streams");
  std::mt19937 urbg(20260817);
  int faces[6] = {};
  bool in_range = true;
  double sum = 0, square_sum = 0;
  for (int i = 0; i < 60000; ++i) {
    const int face = absl::Uniform(urbg, 0, 6);
    in_range = in_range && face >= 0 && face < 6;
    if (in_range) ++faces[face];
    const double x = absl::Gaussian<double>(urbg, 10.0, 2.0);
    sum += x;
    square_sum += x * x;
  }
  bool balanced = true;
  for (int count : faces) balanced = balanced && count > 9000 && count < 11000;
  const double mean = sum / 60000, variance = square_sum / 60000 - mean * mean;
  check(in_range && balanced, "Uniform covers [0, 6) evenly");
  check(std::fabs(mean - 10.0) < 0.05 && std::fabs(variance - 4.0) < 0.2, "Gaussian moments");
  // absl::SeedSeq salts std::seed_seq's output with MixIntoSeedMaterial.
  std::seed_seq plain{4, 5, 6};
  absl::SeedSeq salted_a{4, 5, 6};
  absl::SeedSeq salted_b{4, 5, 6};
  uint32_t out_plain[8], out_a[8], out_b[8];
  plain.generate(out_plain, out_plain + 8);
  salted_a.generate(out_a, out_a + 8);
  salted_b.generate(out_b, out_b + 8);
  bool salted_equal = true, salted_differs = false;
  for (int i = 0; i < 8; ++i) {
    salted_equal = salted_equal && out_a[i] == out_b[i];
    salted_differs = salted_differs || out_a[i] != out_plain[i];
  }
  check(salted_equal && salted_differs, "SeedSeq mixes in its salt");

  // Patch 0001: Abseil's own types, whatever this file's standard.
#if ABSL_OPTION_USE_STD_SOURCE_LOCATION != 0 || ABSL_OPTION_USE_STD_ORDERING != 0 || \
    defined(ABSL_USES_STD_SOURCE_LOCATION) || defined(ABSL_USES_STD_ORDERING)
  check(false, "SourceLocation and the ordering types are Abseil's");
#endif
  const absl::SourceLocation here = absl::SourceLocation::current();
  check(here.line() > 0 && absl::string_view(here.file_name()).find("main.cc") !=
                               absl::string_view::npos,
        "SourceLocation");

  std::printf("abseil (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
