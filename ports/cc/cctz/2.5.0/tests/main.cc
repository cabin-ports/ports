#include <chrono>
#include <cstdio>
#include <string>

#include "cctz/civil_time.h"
#include "cctz/time_zone.h"

static int failures = 0;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                           \
    }                                                       \
  } while (0)

int main() {
  // Civil-time arithmetic and calendar queries.
  cctz::civil_day d(2015, 2, 28);
  ++d;
  CHECK(d == cctz::civil_day(2015, 3, 1));
  CHECK(cctz::civil_day(2016, 3, 1) - cctz::civil_day(2016, 2, 1) == 29);
  CHECK(cctz::get_weekday(cctz::civil_day(2016, 1, 28)) ==
        cctz::weekday::thursday);
  CHECK(cctz::get_yearday(cctz::civil_day(2015, 12, 31)) == 365);
  CHECK(cctz::civil_second(2015, 1, 2, 3, 4, 60) ==
        cctz::civil_second(2015, 1, 2, 3, 5, 0));

  // Conversion in UTC and in a fixed-offset zone.
  const cctz::time_zone utc = cctz::utc_time_zone();
  const auto tp = cctz::convert(cctz::civil_second(2015, 1, 2, 3, 4, 5), utc);
  CHECK(tp.time_since_epoch().count() == 1420167845);
  const cctz::time_zone jst = cctz::fixed_time_zone(std::chrono::hours(9));
  const cctz::civil_second cs = cctz::convert(tp, jst);
  CHECK(cs == cctz::civil_second(2015, 1, 2, 12, 4, 5));

  // Formatting and parsing.
  CHECK(cctz::format("%Y-%m-%d %H:%M:%S %z", tp, jst) ==
        "2015-01-02 12:04:05 +0900");
  cctz::time_point<cctz::seconds> tp2;
  CHECK(cctz::parse("%Y-%m-%d %H:%M:%S", "2015-01-02 03:04:05", utc, &tp2));
  CHECK(tp2 == tp);
  CHECK(!cctz::parse("%Y-%m-%d", "not a date", utc, &tp2));
  cctz::time_point<cctz::seconds> tp3;
  CHECK(cctz::parse("%Y-%m-%d %H:%M:%S %z", "2015-01-02 12:04:05 +0900", utc,
                    &tp3));
  CHECK(tp3 == tp);

  cctz::time_zone tz;
  CHECK(cctz::load_time_zone("UTC", &tz));
  CHECK(tz == utc);

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
