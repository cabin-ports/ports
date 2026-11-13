#include <croncpp.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace {

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

// Times are local, through mktime; starts are at noon or later in the day.
template <typename Traits = cron::cron_standard_traits>
std::string next_from(const std::string& expr, const std::string& from) {
  return cron::utils::to_string(
      cron::cron_next<Traits>(cron::make_cron<Traits>(expr),
                              cron::utils::to_tm(from)));
}

std::time_t to_time(const std::string& text) {
  std::tm tm = cron::utils::to_tm(text);
  return cron::utils::tm_to_time(tm);
}

using ms_time =
    std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;

ms_time at_ms(const std::string& text, int ms) {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::from_time_t(to_time(text))) +
         std::chrono::milliseconds(ms);
}

template <typename Traits = cron::cron_standard_traits>
bool rejects(const std::string& expr) {
  try {
    cron::make_cron<Traits>(expr);
  } catch (const cron::bad_cronexpr&) {
    return true;
  }
  return false;
}

void test_standard() {
  check(next_from("0 30 20 * * *", "2018-08-08 20:00:00") ==
            "2018-08-08 20:30:00",
        "next occurrence later the same day");
  check(next_from("0 30 20 * * *", "2018-08-08 20:30:00") ==
            "2018-08-09 20:30:00",
        "next occurrence is strictly after the start");
  check(next_from("0 0 12 ? * MON-FRI", "2018-08-10 12:00:00") ==
            "2018-08-13 12:00:00",
        "weekday range skips the weekend");

  const cron::cronexpr every_quarter = cron::make_cron("0 */15 * * * *");
  check(cron::cron_next(every_quarter, to_time("2018-08-08 10:07:00")) ==
            to_time("2018-08-08 10:15:00"),
        "time_t overload of cron_next");
  check(cron::cron_prev(every_quarter, to_time("2018-08-08 10:15:00")) ==
            to_time("2018-08-08 10:00:00"),
        "time_t overload of cron_prev is strictly earlier");
  check(cron::utils::to_string(cron::cron_prev(
            cron::make_cron("0 30 20 * * *"),
            cron::utils::to_tm("2018-08-09 12:00:00"))) ==
            "2018-08-08 20:30:00",
        "tm overload of cron_prev");

  // Fractions of a second: cron_next drops them, while cron_next_ceil and the
  // time_point cron_prev round them up to the next whole second.
  check(cron::cron_next(every_quarter, at_ms("2018-08-08 10:14:59", 400)) ==
            at_ms("2018-08-08 10:15:00", 0),
        "time_point overload of cron_next");
  check(cron::cron_next_ceil(every_quarter,
                             at_ms("2018-08-08 10:14:59", 400)) ==
            at_ms("2018-08-08 10:30:00", 0),
        "cron_next_ceil");
  check(cron::cron_prev(every_quarter, at_ms("2018-08-08 10:15:00", 400)) ==
            at_ms("2018-08-08 10:15:00", 0),
        "time_point overload of cron_prev");
}

void test_special_characters() {
  // May 2011 has Fridays on the 6th, 13th, 20th and 27th; July 2011 has a
  // fifth one on the 29th.
  check(next_from("0 0 12 ? * 5#2", "2011-04-30 12:00:00") ==
            "2011-05-13 12:00:00",
        "# selects the second Friday");
  check(next_from("0 0 12 ? * FRI#2", "2011-04-30 12:00:00") ==
            "2011-05-13 12:00:00",
        "# with a day name");
  check(next_from("0 0 12 ? * 5#5", "2011-04-30 12:00:00") ==
            "2011-07-29 12:00:00",
        "# skips months without a fifth Friday");
  // Quartz numbers Sunday 1, so Friday is 6.
  check(next_from<cron::cron_quartz_traits>("0 0 12 ? * 6#2",
                                            "2011-04-30 12:00:00") ==
            "2011-05-13 12:00:00",
        "quartz day numbering");

  check(next_from("0 0 12 L * ?", "2012-02-01 12:00:00") ==
            "2012-02-29 12:00:00",
        "L is the last day of a leap February");
  // 2011-05-15 is a Sunday; 2011-04-30 a Saturday.
  check(next_from("0 0 12 15W * ?", "2011-05-01 12:00:00") ==
            "2011-05-16 12:00:00",
        "W moves a Sunday to the Monday");
  check(next_from("0 0 12 LW * ?", "2011-04-01 12:00:00") ==
            "2011-04-29 12:00:00",
        "LW is the last weekday");
  // June 2011's last Friday, the 24th, has passed.
  check(next_from("0 0 12 ? * 5L", "2011-06-30 12:00:00") ==
            "2011-07-29 12:00:00",
        "5L is the last Friday");
}

void test_day_field_rules() {
  // With both day fields restricted, standard traits match either one: the
  // 1st of January (a Friday in 2021) or any Monday in January.
  check(next_from("0 0 12 1 1 1", "2020-10-08 12:00:00") ==
            "2021-01-01 12:00:00",
        "either day field: the day of month");
  check(next_from("0 0 12 1 1 1", "2021-01-01 12:00:00") ==
            "2021-01-04 12:00:00",
        "either day field: the day of week");
  // Oracle numbers the months from 0, so 4 is May.
  check(next_from<cron::cron_oracle_traits>("0 0 12 1 4 ?",
                                            "2011-01-01 12:00:00") ==
            "2011-05-01 12:00:00",
        "oracle month numbering");
}

void test_years() {
  const cron::cronexpr in_2005 = cron::make_cron("0 15 10 * * ? 2005");
  check(cron::cron_next(in_2005, to_time("2004-06-01 12:00:00")) ==
            to_time("2005-01-01 10:15:00"),
        "year field moves to the first matching year");
  check(cron::cron_next(in_2005, to_time("2020-06-01 12:00:00")) ==
            cron::INVALID_TIME,
        "no occurrence after the last year");
  check(cron::cron_next(in_2005, at_ms("2020-06-01 12:00:00", 0)) ==
            (ms_time::min)(),
        "time_point overload signals no occurrence with min()");
}

void test_parse() {
  check(rejects("bad"), "too few fields");
  check(rejects("60 * * * * *"), "second out of range");
  check(rejects("0 0 12 * * MON-XYZ"), "unknown day name");
  check(rejects<cron::cron_quartz_traits>("0 0 0 1 1 1"),
        "quartz rejects both day fields restricted");
  check(rejects("0 0 0 30 2 *"), "30 February never arrives");

  check(cron::to_cronstr(cron::make_cron("0 30 23 ? * 5#2")) ==
            "0 30 23 ? * 5#2",
        "to_cronstr keeps the original text");
  check(cron::make_cron("0 0 12 ? * MON-FRI") ==
            cron::make_cron("0 0 12 ? * 1-5"),
        "day names equal numbers");
  check(cron::make_cron("0 0 12 15W * ?") != cron::make_cron("0 0 12 15 * ?"),
        "W changes the expression");
}

}  // namespace

int main() {
  test_standard();
  test_special_characters();
  test_day_field_rules();
  test_years();
  test_parse();
  std::printf("croncpp 2026.8.12 ok (__cplusplus %ld)\n",
              static_cast<long>(__cplusplus));
  return 0;
}
