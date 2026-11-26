/* ccronexpr from C: parse six-field expressions and compute the next and
   previous UTC instants around checked epoch values; reject a bad one. */
#include <stdio.h>

#include <ccronexpr.h>

static int failures = 0;

static void check(int ok, int line, const char *what) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond) != 0, __LINE__, #cond)

static int parse(cron_expr *e, const char *text) {
  const char *err = NULL;
  cron_parse_expr(text, e, &err);
  return err == NULL;
}

int main(void) {
  cron_expr e;

  /* 2021-01-01T12:00:00Z is a Friday; 2021-01-04 is the next Monday. */
  CHECK(parse(&e, "0 30 9 * * MON-FRI"));
  CHECK((long long)cron_next(&e, 1609502400) == 1609752600LL);
  CHECK((long long)cron_prev(&e, 1609502400) == 1609493400LL);

  CHECK(parse(&e, "*/15 * * * * *"));
  CHECK((long long)cron_next(&e, 1577836807) == 1577836815LL);
  CHECK((long long)cron_prev(&e, 1577836807) == 1577836800LL);

  /* Last day of the leap-year February, and a leap day four years on. */
  CHECK(parse(&e, "0 0 0 L * *"));
  CHECK((long long)cron_next(&e, 1581292800) == 1582934400LL);
  CHECK(parse(&e, "0 0 0 29 2 *"));
  CHECK((long long)cron_next(&e, 1609459200) == 1709164800LL);

  CHECK(!parse(&e, "not a cron expression"));
  CHECK(!parse(&e, "0 0 25 * * *"));

  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("ok");
  return 0;
}
