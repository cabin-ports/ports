/* C++ consumer test, adding MTR_SCOPE: trace events of every phase the macros emit go through
   init, flush and shutdown, then the JSON file is read back and its lines
   are compared exactly, apart from the pid, tid and timestamp. */
#include <minitrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what, int line)
{
  if (!ok) {
    fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond) ? 1 : 0, #cond, __LINE__)

/* Copy line N (0-based) of TEXT into LINE without its trailing comma;
   0 when there is no such line. */
static int get_line(const char *text, int n, char *line, size_t size)
{
  const char *end;
  size_t len;
  for (; n > 0; --n) {
    text = strchr(text, '\n');
    if (text == NULL) return 0;
    ++text;
  }
  end = strchr(text, '\n');
  if (end == NULL) return 0;
  len = (size_t)(end - text);
  if (len > 0 && text[len - 1] == ',') --len;
  if (len >= size) return 0;
  memcpy(line, text, len);
  line[len] = '\0';
  return 1;
}

/* Line N must be {"cat":CAT,"pid":..,"tid":..,"ts":..,"ph":REST. */
static void check_event(const char *text, int n, const char *cat,
                        const char *rest)
{
  char want[64];
  char line[1024];
  const char *p;
  int found = get_line(text, n, line, sizeof line);
  CHECK(found);
  if (!found) return;
  snprintf(want, sizeof want, "{\"cat\":\"%s\",\"pid\":", cat);
  CHECK(strncmp(line, want, strlen(want)) == 0);
  p = strstr(line, ",\"ts\":");
  CHECK(p != NULL && strtol(p + 6, NULL, 10) >= 0);
  p = strstr(line, ",\"ph\":");
  CHECK(p != NULL);
  if (p != NULL) {
    if (strcmp(p + 6, rest) != 0)
      fprintf(stderr, "event %d: got %s, want %s\n", n, p + 6, rest);
    CHECK(strcmp(p + 6, rest) == 0);
  }
}

int main(void)
{
  static char text[65536];
  char path[1024];
  char dynamic[32];
  const char *dir;
  const char *pooled1, *pooled2;
  size_t got;
  FILE *f;

  /* Among the probe's own build outputs: `cabin run` keeps the caller's
     working directory, which need not be writable.  mtr_init crashes on a
     file it cannot open. */
  dir = getenv("CABIN_PROFILE_DIR");
  if (dir == NULL) return 1;
  snprintf(path, sizeof path, "%s/minitrace-consumer-test.json", dir);
  mtr_init(path);
  MTR_META_PROCESS_NAME("probe");
  MTR_BEGIN_I("test", "work", "answer", 42);
  MTR_INSTANT("test", "tick");
  MTR_INSTANT_C("test", "note", "kind", "hello");
  MTR_COUNTER("test", "items", 7);
  strcpy(dynamic, "dyn-");
  strcat(dynamic, "amic");
  MTR_BEGIN_S("test", "inner", "who", dynamic);
  strcpy(dynamic, "overwritten");
  MTR_END_C("test", "inner", "state", "done");
  MTR_END("test", "work");
  {
    MTR_SCOPE("test", "scoped");
  }

  pooled1 = mtr_pool_string("pooled");
  pooled2 = mtr_pool_string("pooled");
  CHECK(pooled1 == pooled2);
  CHECK(strcmp(pooled1, "pooled") == 0);
  CHECK(mtr_time_s() >= 0.0);

  mtr_flush();
  MTR_INSTANT("test", "after-flush");
  mtr_shutdown();

  f = fopen(path, "rb");
  CHECK(f != NULL);
  if (f == NULL) return 1;
  got = fread(text, 1, sizeof text - 1, f);
  fclose(f);
  text[got] = '\0';
  CHECK(got > 0 && got < sizeof text - 1);
  CHECK(remove(path) == 0);

  CHECK(strncmp(text, "{\"traceEvents\":[\n", 17) == 0);
  CHECK(got > 4 && strcmp(text + got - 4, "\n]}\n") == 0);
  /* Line 0 is the header, so the first event is line 1. */
  check_event(text, 1, "", "\"M\",\"name\":\"process_name\",\"args\":{\"name\":\"probe\"}}");
  check_event(text, 2, "test", "\"B\",\"name\":\"work\",\"args\":{\"answer\":42}}");
  check_event(text, 3, "test", "\"I\",\"name\":\"tick\",\"args\":{}}");
  check_event(text, 4, "test", "\"I\",\"name\":\"note\",\"args\":{\"kind\":\"hello\"}}");
  check_event(text, 5, "test", "\"C\",\"name\":\"items\",\"args\":{\"items\":7}}");
  check_event(text, 6, "test", "\"B\",\"name\":\"inner\",\"args\":{\"who\":\"dyn-amic\"}}");
  check_event(text, 7, "test", "\"E\",\"name\":\"inner\",\"args\":{\"state\":\"done\"}}");
  check_event(text, 8, "test", "\"E\",\"name\":\"work\",\"args\":{}}");
  {
    /* The scope's X event carries its duration after the arguments. */
    char line[1024];
    const char *dur;
    CHECK(get_line(text, 9, line, sizeof line));
    dur = strstr(line, ",\"ph\":\"X\",\"name\":\"scoped\",\"args\":{},\"dur\":");
    CHECK(dur != NULL && strncmp(line, "{\"cat\":\"test\",\"pid\":", 20) == 0);
    if (dur != NULL) {
      char *end;
      dur += strlen(",\"ph\":\"X\",\"name\":\"scoped\",\"args\":{},\"dur\":");
      CHECK(strtol(dur, &end, 10) >= 0 && end != dur && strcmp(end, "}") == 0);
    }
  }
  check_event(text, 10, "test", "\"I\",\"name\":\"after-flush\",\"args\":{}}");
  CHECK(strcmp(strstr(text, "]}"), "]}\n") == 0);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
