#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* After stdio.h: xchange.h uses size_t without declaring it. */
#include <xchange.h>
#include <xjson.h>

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond);    \
      failures++;                                                         \
    }                                                                     \
  } while (0)

static const char expected[] =
    "{\n"
    "  \"name\": \"probe \\\"x\\\"\\n\",\n"
    "  \"count\": 42,\n"
    "  \"big\": 12345678901,\n"
    "  \"ratio\": 2.5,\n"
    "  \"ok\": true,\n"
    "  \"dims\": [ 1, 2, 3 ],\n"
    "  \"inner\": {\n"
    "    \"id\": 7\n"
    "  }\n"
    "}\n";

int main(void) {
  XStructure *s = xCreateStruct();
  XStructure *inner = xCreateStruct();
  int dims[] = {1, 2, 3};
  char *json;
  char *again;
  char *tail = NULL;
  XStructure *parsed;
  XStructure *copy;
  XStructure *dup;
  XField *f;
  XLookupTable *tab;
  char *escaped;
  char *unescaped;
  char *end;
  float flt;
  double dbl;

  xSetField(s, xCreateStringField("name", "probe \"x\"\n"));
  xSetField(s, xCreateIntField("count", 42));
  xSetField(s, xCreateLongField("big", 12345678901LL));
  xSetField(s, xCreateDoubleField("ratio", 2.5));
  xSetField(s, xCreateBooleanField("ok", TRUE));
  xSetField(s, xCreate1DField("dims", X_INT, 3, dims));
  xSetField(inner, xCreateIntField("id", 7));
  xSetSubstruct(s, "inner", inner);

  json = xjsonToString(s);
  CHECK(json != NULL && strcmp(json, expected) == 0);
  if (json != NULL && strcmp(json, expected) != 0) {
    fprintf(stderr, "got:\n%s", json);
  }

  parsed = xjsonParseString(expected, &tail);
  CHECK(parsed != NULL && tail != NULL && strcmp(tail, "\n") == 0);
  if (parsed == NULL) {
    return 1;
  }
  again = xjsonToString(parsed);
  CHECK(again != NULL && strcmp(again, expected) == 0);
  CHECK(strcmp(xGetStringValue(xGetField(parsed, "name")), "probe \"x\"\n") ==
        0);
  CHECK(xGetAsLong(xGetField(parsed, "count"), -1) == 42);
  CHECK(xGetAsDouble(xGetField(parsed, "big")) == 12345678901.0);
  CHECK(xGetAsDouble(xGetField(parsed, "ratio")) == 2.5);
  f = xGetField(parsed, "ok");
  CHECK(f != NULL && f->type == X_BOOLEAN && *(XBoolean *)f->value == TRUE);
  f = xGetField(parsed, "dims");
  CHECK(f != NULL && f->type == X_INT && f->ndim == 1 && f->sizes[0] == 3 &&
        xGetAsLongAtIndex(f, 2, -1) == 3);
  CHECK(xGetAsLong(xGetField(parsed, "inner:id"), -1) == 7);
  CHECK(xGetField(parsed, "inner:missing") == NULL);
  CHECK(xCountFields(parsed) == 7 && xDeepCountFields(parsed) == 8);

  tab = xCreateLookup(parsed, TRUE);
  CHECK(tab != NULL && xLookupCount(tab) == 8);
  CHECK(xGetAsLong(xLookupField(tab, "inner:id"), -1) == 7);
  CHECK(xLookupField(tab, "id") == NULL);
  xDestroyLookup(tab);

  copy = xCopyOfStruct(parsed);
  f = xRemoveField(copy, "inner");
  CHECK(f != NULL && xCountFields(copy) == 6 && xCountFields(parsed) == 7);
  xDestroyField(f);
  CHECK(xSortFieldsByName(copy, FALSE) == X_SUCCESS &&
        strcmp(copy->firstField->name, "big") == 0);

  /* A repeated key keeps the last value; tail points past the object. */
  dup = xjsonParseString("{ \"a\": 1, \"a\": \"two\" } rest", &tail);
  CHECK(dup != NULL && xCountFields(dup) == 1 &&
        strcmp(xGetStringValue(xGetField(dup, "a")), "two") == 0 &&
        strcmp(tail, " rest") == 0);
  CHECK(xjsonParseString("[ 1, 2 ]", NULL) == NULL);

  CHECK(xParseBoolean(" YES", &end) == TRUE && *end == '\0');
  CHECK(xParseBoolean("Off", NULL) == FALSE);
  CHECK(xParseBoolean("0x10", NULL) == TRUE);
  flt = xParseFloat("1e39", &end);
  CHECK(isinf(flt) && flt > 0 && errno == ERANGE && *end == '\0');
  dbl = xParseDouble("-inf", &end);
  CHECK(isinf(dbl) && dbl < 0 && *end == '\0');
  /* Just above halfway between 1 and the next float.  strtof, which
     xchange.c takes on Linux with the define in cabin.toml, rounds it up;
     elsewhere it goes through a double, whose tie rounds to even, to 1. */
  flt = xParseFloat("1.0000000596046447753906250001", NULL);
#ifdef __linux__
  CHECK(flt == 1.00000011920928955078125f);
#else
  CHECK(flt == 1.0f);
#endif

  escaped = xjsonEscape("tab\there \"q\"", 0);
  CHECK(escaped != NULL && strcmp(escaped, "tab\\there \\\"q\\\"") == 0);
  unescaped = escaped != NULL ? xjsonUnescape(escaped) : NULL;
  CHECK(unescaped != NULL && strcmp(unescaped, "tab\there \"q\"") == 0);

  free(json);
  free(again);
  free(escaped);
  free(unescaped);
  xDestroyStruct(s);
  xDestroyStruct(parsed);
  xDestroyStruct(copy);
  xDestroyStruct(dup);

  if (failures != 0) {
    return 1;
  }
  printf("libxchange: JSON round-trips through XStructure\n");
  return 0;
}
