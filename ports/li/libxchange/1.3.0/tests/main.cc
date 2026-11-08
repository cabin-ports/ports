// The C API from C++.  The headers declare no extern "C", so the includes
// are wrapped here, after stddef.h for the size_t that xchange.h uses
// without declaring it.
#include <stddef.h>

extern "C" {
#include <xchange.h>
#include <xjson.h>
}

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

struct Free {
  void operator()(char *p) const { std::free(p); }
};
using CString = std::unique_ptr<char, Free>;

bool equals(const CString &got, const std::string &want) {
  return got != nullptr && want == got.get();
}

}  // namespace

int main() {
  XStructure *s = xjsonParseString(
      "{ \"list\": [ { \"v\": 1 }, { \"v\": 2, \"w\": \"x\" } ], \"pi\": 3.25 }",
      nullptr);
  if (s == nullptr) {
    std::fprintf(stderr, "FAIL: the document does not parse\n");
    return 1;
  }

  XField *list = xGetField(s, "list");
  check(list != nullptr && list->type == X_STRUCT && list->ndim == 1 &&
            list->sizes[0] == 2 && xGetFieldCount(list) == 2,
        "list holds two structures");
  if (list != nullptr && list->type == X_STRUCT) {
    XStructure *items = static_cast<XStructure *>(list->value);
    check(xGetAsLong(xGetField(&items[1], "v"), -1) == 2 &&
              std::string(xGetStringValue(xGetField(&items[1], "w"))) == "x",
          "the second structure's fields");
  }
  check(xDeepCountFields(s) == 5, "five fields in all");
  check(xGetAsDouble(xGetField(s, "pi")) == 3.25, "pi is 3.25");
  check(equals(CString(xGetAggregateID("list", "v")), "list:v"),
        "aggregate IDs join with ':'");

  const double xs[] = {0.5, -1.25};
  XField *f = xCreate1DField("xs", X_DOUBLE, 2, xs);
  check(equals(CString(xjsonFieldToString(f)), "\"xs\": [ 0.5, -1.25 ]\n"),
        "a field's JSON");
  check(xGetAsDoubleAtIndex(f, 1) == -1.25, "the second element");
  check(xSetField(s, f) == nullptr && xCountFields(s) == 3,
        "a new name adds a field");

  XField *old = xSetField(s, xCreateStringField("pi", "approx"));
  check(old != nullptr && xGetAsDouble(old) == 3.25 && xCountFields(s) == 3,
        "a known name replaces the field and returns the old one");
  xDestroyField(old);
  check(std::string(xGetStringValue(xGetField(s, "pi"))) == "approx",
        "the replacement");

  char *end = nullptr;
  check(xParseDouble("  6.25e-1x", &end) == 0.625 && *end == 'x',
        "xParseDouble stops at the first non-number");

  xDestroyStruct(s);

  if (failures != 0) {
    return 1;
  }
  std::printf("libxchange from C++: nested arrays of structures parse\n");
  return 0;
}
