// windows.h first: its min/max macros must not break the headers.
#ifdef _WIN32
#include <windows.h>
#endif
#include <datetime/datetime.h>

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>

using jed_utils::datetime;
using jed_utils::timespan;

static int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << __FILE__ << ":" << __LINE__ << ": " #cond << "\n";    \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

static bool same(const datetime& d, int y, int mo, int day, int h, int mi, int s) {
  return d.get_year() == y && d.get_month() == mo && d.get_day() == day &&
         d.get_hour() == h && d.get_minute() == mi && d.get_second() == s;
}

int main() {
  // The library adds days as 86400 s through mktime, so fix the zone.
#ifdef _WIN32
  _putenv_s("TZ", "UTC");
  _tzset();
#else
  setenv("TZ", "UTC", 1);
  tzset();
#endif
  // Construction, field access, formatting.
  datetime d(2024, 2, 29, 13, 5, 9);
  CHECK(same(d, 2024, 2, 29, 13, 5, 9));
  CHECK(d.get_weekday() == jed_utils::thursday);
  CHECK(d.to_string() == "2024-02-29 13:05:09");
  CHECK(d.to_shortdate_string() == "2024-02-29");
  CHECK(d.to_string("dd/MM/yyyy hh:mm:ss tt") == "29/02/2024 01:05:09 PM");
  CHECK(datetime::is_leapyear(2024) && !datetime::is_leapyear(2023) &&
        !datetime::is_leapyear(1900) && datetime::is_leapyear(2000));
  try {
    datetime bad(2023, 13, 1);
    CHECK(false);
  } catch (const std::invalid_argument&) {
  }

  // Add and subtract with rollover.
  datetime a(2023, 12, 31, 23, 59, 59);
  a.add_seconds(1);
  CHECK(same(a, 2024, 1, 1, 0, 0, 0));
  datetime b(2024, 2, 28);
  b.add_days(1);
  CHECK(same(b, 2024, 2, 29, 0, 0, 0));
  b.add_days(1);
  CHECK(same(b, 2024, 3, 1, 0, 0, 0));
  b.add_days(-2);
  CHECK(same(b, 2024, 2, 28, 0, 0, 0));
  datetime c(2023, 2, 28);
  c.add_days(1);
  CHECK(same(c, 2023, 3, 1, 0, 0, 0));
  datetime m(2023, 2, 15, 10, 30);
  m.add_months(11);
  CHECK(same(m, 2024, 1, 15, 10, 30, 0));
  m.add_years(-1);
  CHECK(same(m, 2023, 1, 15, 10, 30, 0));
  m.add_hours(15);
  m.add_minutes(45);
  CHECK(same(m, 2023, 1, 16, 2, 15, 0));

  // Comparison and difference.
  datetime early(2024, 2, 28, 12);
  datetime late(2024, 3, 1);
  CHECK(early < late && late > early && early <= late && late >= early);
  CHECK(early != late && early == datetime(2024, 2, 28, 12));
  timespan diff = late - early;
  CHECK(diff.get_days() == 1 && diff.get_hours() == 12);
  CHECK(diff.get_total_hours() == 36 && diff.get_total_seconds() == 129600);

  // Parsing.
  datetime p = datetime::parse("yyyy-MM-dd HH:mm:ss", "2021-07-04 09:08:07");
  CHECK(same(p, 2021, 7, 4, 9, 8, 7));
  datetime q = datetime::parse("dd/MM/yyyy hh:mm tt", "05/11/2019 03:20 PM");
  CHECK(same(q, 2019, 11, 5, 15, 20, 0));

  // Timespan.
  timespan t(1, 2, 3, 4);
  CHECK(t.get_days() == 1 && t.get_hours() == 2 && t.get_minutes() == 3 &&
        t.get_seconds() == 4);
  CHECK(t.get_total_minutes() == 1563 && t.get_total_seconds() == 93784);
  CHECK(t == timespan(1, 2, 3, 4) && t < timespan(2) && timespan(2) > t &&
        t != timespan(1) && t >= timespan(1) && t <= timespan(1, 2, 3, 4));

  if (failures == 0) std::cout << "ok\n";
  return failures != 0;
}
