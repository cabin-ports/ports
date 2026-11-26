#include "NamedType/named_type.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace {

using Meter = fluent::NamedType<int, struct MeterTag, fluent::BinaryAddable,
                                fluent::BinarySubtractable, fluent::Comparable,
                                fluent::Printable, fluent::Hashable>;
using Signed = fluent::NamedType<int, struct SignedTag, fluent::UnaryAddable,
                                 fluent::UnarySubtractable>;
using Name = fluent::NamedType<std::string, struct NameTag>;
using NameRef = fluent::NamedType<std::string&, struct NameRefTag>;
using StrongInt = fluent::NamedType<int, struct StrongIntTag>;
using FirstName = fluent::NamedType<std::string, struct FirstNameTag>;
using LastName = fluent::NamedType<std::string, struct LastNameTag>;

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << __LINE__ << ": " #cond "\n";                                \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void rename(NameRef name) { name.get() = "renamed"; }
void addOne(StrongInt::ref value) { ++value.get(); }

} // namespace

int main() {
  Name name("alice");
  CHECK(name.get() == "alice");
  name.get() += "!";
  CHECK(name.get() == "alice!");
  StrongInt dflt;
  dflt.get() = 7;
  CHECK(dflt.get() == 7);

  Meter a(10), b(4);
  CHECK((a + b).get() == 14);
  CHECK((a - b).get() == 6);
  CHECK((+Signed(10)).get() == 10);
  CHECK((-Signed(10)).get() == -10);
  a += b;
  CHECK(a.get() == 14);
  CHECK(a > b);
  CHECK(b < a);
  CHECK(a != b);
  CHECK(a == Meter(14));
  CHECK(b <= Meter(4));

  std::ostringstream out;
  out << a << ' ' << b;
  CHECK(out.str() == "14 4");

  std::unordered_set<Meter> set{Meter(1), Meter(2), Meter(1)};
  CHECK(set.size() == 2);
  CHECK(set.count(Meter(2)) == 1);
  CHECK(set.count(Meter(3)) == 0);

  std::string original = "bob";
  rename(NameRef(original));
  CHECK(original == "renamed");
  int n = 41;
  addOne(StrongInt::ref(n));
  CHECK(n == 42);
  StrongInt si(9);
  addOne(si);
  CHECK(si.get() == 10);

  static const FirstName::argument firstName;
  static const LastName::argument lastName;
  auto full = fluent::make_named_arg_function<FirstName, LastName>(
      [](FirstName const &f, LastName const &l) { return f.get() + " " + l.get(); });
  CHECK(full(lastName = "Bond", firstName = "James") == "James Bond");
  CHECK(full(firstName = "Ada", lastName = "Lovelace") == "Ada Lovelace");

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
