#include <confu_json/confu_json.hxx>

#include <boost/fusion/adapted/struct/define_struct.hpp>
#include <boost/json.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

enum class Color { red, green };

BOOST_FUSION_DEFINE_STRUCT((demo), Inner, (long, answer))
BOOST_FUSION_DEFINE_STRUCT((demo), Item,
                           (long, id)(demo::Inner, inner)(std::vector<int>, values)(std::optional<std::string>, name)(Color, color))

static int failures = 0;
#define CHECK(c)                                                              \
  do {                                                                        \
    if (!(c)) {                                                               \
      std::cerr << __FILE__ << ':' << __LINE__ << ": " << #c << '\n';         \
      ++failures;                                                             \
    }                                                                         \
  } while (0)

int main() {
  demo::Item item{};
  item.id = 7;
  item.inner.answer = 42;
  item.values = {1, 2, 3};
  item.name = "x";
  item.color = Color::green;

  auto const json = confu_json::to_json(item);
  CHECK(boost::json::serialize(json) ==
        R"({"id":7,"inner":{"answer":42},"values":[1,2,3],"name":"x","color":"green"})");

  auto back = confu_json::to_object<demo::Item>(json);
  CHECK(back.id == 7);
  CHECK(back.inner.answer == 42);
  CHECK((back.values == std::vector<int>{1, 2, 3}));
  CHECK(back.name == "x");
  CHECK(back.color == Color::green);

  item.name.reset();
  CHECK(confu_json::to_json(item).at("name").is_null());
  CHECK(!confu_json::to_object<demo::Item>(confu_json::to_json(item)).name.has_value());

  boost::system::error_code ec;
  auto parsed = confu_json::read_json(
      R"({"id":9,"inner":{"answer":5},"values":[],"name":null,"color":"red"})", ec);
  CHECK(!ec);
  auto fromString = confu_json::to_object<demo::Item>(parsed);
  CHECK(fromString.id == 9);
  CHECK(fromString.inner.answer == 5);
  CHECK(fromString.values.empty());
  CHECK(!fromString.name.has_value());
  CHECK(fromString.color == Color::red);

  // Malformed text is reported through the error code.
  confu_json::read_json("{\"id\":", ec);
  CHECK(ec);

  CHECK(confu_json::type_name<demo::Item>() == "Item");
  CHECK(confu_json::type_name<std::string>() == "string");

  // A document of the wrong shape throws.
  bool missing = false;
  try {
    confu_json::to_object<demo::Item>(boost::json::parse("{}"));
  } catch (std::exception const &) {
    missing = true;
  }
  CHECK(missing);

  bool wrongKind = false;
  try {
    confu_json::to_object<demo::Item>(boost::json::parse(
        R"({"id":"seven","inner":{"answer":1},"values":[],"name":null,"color":"red"})"));
  } catch (std::exception const &) {
    wrongKind = true;
  }
  CHECK(wrongKind);

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
