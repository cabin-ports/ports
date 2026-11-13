#include <ball_severity.h>
#include <bbldc_basicisma30360.h>
#include <bdldfp_decimal.h>
#include <bdldfp_decimalutil.h>
#include <bdlde_crc32c.h>
#include <bdlpcre_regex.h>
#include <bdlt_date.h>
#include <bdlt_dayofweek.h>
#include <bslma_testallocator.h>

#include <bsl_format.h>
#include <bsl_sstream.h>
#include <bsl_string.h>
#include <bsl_string_view.h>
#include <bsl_vector.h>

#include <cstdio>

using namespace BloombergLP;

static int failures = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

int main() {
  // bsl: allocator-aware containers return every block to their
  // allocator; floating-point formatting runs through bbryu.
  {
    bslma::TestAllocator ta("test");
    {
      bsl::vector<bsl::string> v(&ta);
      v.push_back("a string too long for the short-string buffer");
      CHECK(v[0].get_allocator().mechanism() == &ta);
      CHECK(ta.numBlocksInUse() > 0);
    }
    CHECK(ta.numBlocksInUse() == 0);
  }
  CHECK(bsl::format("{}|{:>4}", 0.1, 42) == "0.1|  42");

  // bdlt: 2024 is a leap year, and 2024-02-29 is a Thursday.
  bdlt::Date date(2024, 2, 28);
  ++date;
  CHECK(date.month() == 2 && date.day() == 29);
  CHECK(date.dayOfWeek() == bdlt::DayOfWeek::e_THU);

  // bdldfp over the Intel decimal library: 0.1 + 0.2 is exactly 0.3.
  bdldfp::Decimal64 a, b, c;
  CHECK(0 == bdldfp::DecimalUtil::parseDecimal64(&a, "0.1"));
  CHECK(0 == bdldfp::DecimalUtil::parseDecimal64(&b, "0.2"));
  CHECK(0 == bdldfp::DecimalUtil::parseDecimal64(&c, "0.3"));
  CHECK(a + b == c);
  bsl::ostringstream os;
  os << a + b;
  CHECK(os.str() == "0.3");

  // bdlde: the CRC-32C check value of "123456789".
  CHECK(bdlde::Crc32c::calculate("123456789", 9) == 0xE3069283u);

  // bdlpcre over cabin-ports/pcre2.
  bdlpcre::RegEx re;
  bsl::string error;
  size_t offset = 0;
  CHECK(0 == re.prepare(&error, &offset, "(\\d+)-(\\d+)"));
  bsl::vector<bsl::string_view> groups;
  CHECK(0 == re.match(&groups, bsl::string_view("ref 12-345")));
  CHECK(groups.size() == 3 && groups[1] == "12" && groups[2] == "345");
  CHECK(0 != re.prepare(&error, &offset, "(unclosed"));

  // bbl: ISMA 30/360 counts every whole month as 30 days.
  CHECK(bbldc::BasicIsma30360::daysDiff(bdlt::Date(2024, 1, 15),
                                        bdlt::Date(2024, 2, 15)) == 30);

  // bal
  ball::Severity::Level level = ball::Severity::e_OFF;
  CHECK(0 == ball::Severity::fromAscii(&level, "warn", 4));
  CHECK(level == ball::Severity::e_WARN);

  if (failures != 0) {
    return 1;
  }
  std::printf("bde ok\n");
  return 0;
}
