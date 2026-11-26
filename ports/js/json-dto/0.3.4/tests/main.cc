#include <json_dto/pub.hpp>
#include <json_dto/validators.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int fail(const char* what) {
  std::fprintf(stderr, "json-dto: %s\n", what);
  return 1;
}

struct msg {
  std::string from;
  std::int64_t when{};
  std::vector<int> v;
  json_dto::nullable_t<std::string> note;

  template <typename Io>
  void json_io(Io& io) {
    io & json_dto::mandatory("from", from)
       & json_dto::mandatory("when", when)
       & json_dto::optional("v", v, std::vector<int>{})
       & json_dto::optional("note", note, nullptr);
  }
};

struct bounded {
  std::int64_t when{};

  template <typename Io>
  void json_io(Io& io) {
    io & json_dto::mandatory("when", when,
                             json_dto::min_max_constraint<std::int64_t>(0, 100));
  }
};

template <typename T>
bool throws_ex(const char* json) {
  try {
    (void)json_dto::from_json<T>(json);
  } catch (const json_dto::ex_t&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  msg m;
  m.from = "ann";
  m.when = 42;
  m.v = {1, 2};
  m.note = std::string("hi");
  if (json_dto::to_json(m) !=
      R"({"from":"ann","when":42,"v":[1,2],"note":"hi"})") {
    return fail("to_json");
  }

  msg empty;
  empty.from = "x";
  // Fields equal to their optional default are omitted.
  if (json_dto::to_json(empty) != R"({"from":"x","when":0})") {
    return fail("to_json defaults");
  }

  const auto r = json_dto::from_json<msg>(R"({"from":"bob","when":7,"note":"yo"})");
  if (r.from != "bob" || r.when != 7 || !r.v.empty() || !r.note ||
      *r.note != "yo") {
    return fail("from_json values");
  }
  const auto r2 = json_dto::from_json<msg>(R"({"from":"c","when":1,"v":[3,4,5]})");
  if (r2.v != std::vector<int>({3, 4, 5}) || r2.note) {
    return fail("from_json optional");
  }

  if (!throws_ex<msg>(R"({"when":1})")) return fail("missing mandatory");
  if (!throws_ex<bounded>(R"({"when":101})")) return fail("constraint violated");
  if (throws_ex<bounded>(R"({"when":100})")) return fail("constraint boundary");
  return 0;
}
