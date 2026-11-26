#include <json_struct/json_struct.h>
#include <json_struct/json_struct_diff.h>

#include <cstdio>
#include <string>
#include <vector>

struct Inner
{
  int id = 0;
  JS_OBJ(id);
};

struct Obj
{
  int One = 0;
  std::string Two;
  double Three = 0;
  std::vector<Inner> Items;
  JS_OBJ(One, Two, Three, Items);
};

#define CHECK(c)                                          \
  do                                                      \
  {                                                       \
    if (!(c))                                             \
    {                                                     \
      std::printf("FAILED line %d: %s\n", __LINE__, #c); \
      return 1;                                           \
    }                                                     \
  } while (0)

int main()
{
  const std::string json = R"({"One":1,"Two":"t\"w\no","Three":3.5,"Items":[{"id":7},{"id":8}]})";
  Obj o;
  JS::ParseContext ctx(json);
  CHECK(ctx.parseTo(o) == JS::Error::NoError);
  CHECK(o.One == 1);
  CHECK(o.Two == "t\"w\no");
  CHECK(o.Three == 3.5);
  CHECK(o.Items.size() == 2 && o.Items[0].id == 7 && o.Items[1].id == 8);

  const std::string out = JS::serializeStruct(o, JS::SerializerOptions(JS::SerializerOptions::Compact));
  CHECK(out == json);

  Obj bad;
  const std::string badJson = R"({"One":"x"})";
  JS::ParseContext badCtx(badJson);
  CHECK(badCtx.parseTo(bad) != JS::Error::NoError);

  const std::string base = R"({"a":1,"b":[1,2]})";
  const std::string same = R"({"a":1,"b":[1,2]})";
  const std::string changed = R"({"a":2,"b":[1,2]})";
  JS::DiffContext dc(base);
  CHECK(dc.error == JS::DiffError::NoError);
  size_t pos = dc.diff(same);
  CHECK(dc.diffs[pos].diff_count == 0);
  pos = dc.diff(changed);
  CHECK(dc.diffs[pos].diff_count == 1);
  CHECK(dc.diffs[pos].diffs[1] == JS::DiffType::ValueDiff);
  return 0;
}
