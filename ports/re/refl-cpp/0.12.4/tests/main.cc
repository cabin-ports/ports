// Consumer test for cabin-ports/refl-cpp: types reflected with REFL_AUTO
// (fields with a custom attribute, property getters and setters, a plain
// member function, a base class), checking exact member names, attribute
// queries, and reads, writes and calls through the descriptors.
#include <refl.hpp>

#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

struct Serializable : refl::attr::usage::field {};

struct Point {
  int x;
  int y;
  int cache;
};
REFL_AUTO(type(Point), field(x, Serializable()), field(y, Serializable()), field(cache))

class Account {
 public:
  explicit Account(std::string owner) : owner_(std::move(owner)), balance_(0) {}
  const std::string &get_owner() const { return owner_; }
  int get_balance() const { return balance_; }
  void set_balance(int balance) { balance_ = balance; }
  int deposit(int amount) { return balance_ += amount; }

 private:
  std::string owner_;
  int balance_;
};
REFL_AUTO(type(Account), func(get_owner, property()), func(get_balance, property()), func(set_balance, property()),
          func(deposit))

struct Shape {
  int sides;
};
struct Square : Shape {
  int side;
};
REFL_AUTO(type(Shape), field(sides))
REFL_AUTO(type(Square, bases<Shape>), field(side))

struct NotReflected {};

constexpr auto point = refl::reflect<Point>();
static_assert(refl::is_reflectable<Point>() && !refl::is_reflectable<NotReflected>(), "is_reflectable");
static_assert(decltype(point.members)::size == 3, "three reflected fields");
static_assert(point.name == "Point", "the type's name");
static_assert(refl::descriptor::get_display_name_const(refl::trait::get_t<1, refl::member_list<Point>>{}) == "y",
              "member order");
static_assert(std::is_same_v<decltype(refl::descriptor::get_base_types(refl::reflect<Square>())),
                             refl::util::type_list<Shape>>,
              "bases<>");

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void test_fields() {
  Point p{3, 4, 99};
  std::vector<std::string> names;
  int sum = 0;
  refl::util::for_each(point.members, [&](auto member) {
    names.push_back(refl::descriptor::get_display_name(member));
    sum += member(p);
  });
  check(names == std::vector<std::string>{"x", "y", "cache"} && sum == 106, "for_each visits the fields in order");

  constexpr auto serialized =
      refl::util::filter(point.members, [](auto member) { return refl::descriptor::has_attribute<Serializable>(member); });
  std::string keys;
  refl::util::for_each(serialized, [&](auto member) { keys += std::string(member.name.c_str()) + ";"; });
  check(serialized.size == 2 && keys == "x;y;", "filter by a custom attribute");

  constexpr auto y = refl::util::find_one(point.members, [](auto member) { return member.name == "y"; });
  static_assert(refl::descriptor::is_field(y) && refl::descriptor::is_writable(y), "y is a writable field");
  y(p) = 10;
  check(p.y == 10 && p.x == 3, "write through a field descriptor");
}

static void test_properties() {
  constexpr auto members = refl::member_list<Account>{};
  constexpr auto getter = refl::util::find_one(members, [](auto member) { return member.name == "get_balance"; });
  constexpr auto deposit = refl::util::find_one(members, [](auto member) { return member.name == "deposit"; });
  static_assert(refl::descriptor::is_property(getter) && !refl::descriptor::is_property(deposit), "property()");
  static_assert(refl::descriptor::is_function(deposit), "deposit is a function");
  constexpr auto setter = refl::descriptor::get_writer(getter);
  static_assert(setter.name == "set_balance", "the getter's writer is the setter");

  Account account("ada");
  check(std::string(refl::descriptor::get_display_name(getter)) == "balance", "a property's display name");
  setter(account, 50);
  check(getter(account) == 50, "set and get through the property descriptors");
  check(deposit(account, 25) == 75 && account.get_balance() == 75, "call a member function");

  std::vector<std::string> properties;
  refl::util::for_each(members, [&](auto member) {
    if constexpr (refl::descriptor::is_property(member)) properties.push_back(refl::descriptor::get_display_name(member));
  });
  check(properties == std::vector<std::string>{"owner", "balance", "balance"}, "every property's display name");
}

static void test_bases() {
  // member_list holds the declared members first, then the bases' members.
  static_assert(refl::declared_member_list<Square>::size == 1 && refl::member_list<Square>::size == 2,
                "inherited members");
  Square s;
  s.sides = 4;
  s.side = 7;
  std::string names;
  int total = 0;
  refl::util::for_each(refl::member_list<Square>{}, [&](auto member) {
    names += std::string(member.name.c_str()) + ";";
    total += member(s);
  });
  check(names == "side;sides;" && total == 11, "the derived type's fields, then the base's");
}

int main() {
  test_fields();
  test_properties();
  test_bases();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("refl-cpp 0.12.4 C++ ok");
  return 0;
}
