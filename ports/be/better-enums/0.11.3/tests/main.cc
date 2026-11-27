#include <enum.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>

BETTER_ENUM(Channel, int, Red, Green = 5, Blue)

static int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);         \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  Channel c = Channel::Green;
  CHECK(c == +Channel::Green);
  CHECK(c._to_integral() == 5);
  CHECK(std::strcmp(c._to_string(), "Green") == 0);
  CHECK((+Channel::Blue)._to_integral() == 6);

  CHECK(Channel::_size() == 3);
  CHECK(std::strcmp(Channel::_names()[2], "Blue") == 0);
  int sum = 0;
  for (Channel::_value_iterable::iterator it = Channel::_values().begin();
       it != Channel::_values().end(); ++it)
    sum += (*it)._to_integral();
  CHECK(sum == 0 + 5 + 6);

  CHECK(Channel::_from_string("Blue") == +Channel::Blue);
  CHECK(Channel::_from_integral(0) == +Channel::Red);
  CHECK(Channel::_is_valid(5) && !Channel::_is_valid(1));
  CHECK(Channel::_is_valid("Red") && !Channel::_is_valid("red"));
  CHECK(!Channel::_from_string_nothrow("Purple"));
  CHECK(*Channel::_from_string_nothrow("Green") == +Channel::Green);
  CHECK(Channel::_from_string_nocase("rED") == +Channel::Red);

  bool threw = false;
  try {
    Channel::_from_string("Purple");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    Channel::_from_integral(1);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);

  switch (c) {
    case Channel::Green: break;
    default: CHECK(!"switch took the wrong case");
  }

  std::ostringstream out;
  out << +Channel::Blue;
  CHECK(out.str() == "Blue");
  std::istringstream in("Red");
  Channel read = +Channel::Blue;
  in >> read;
  CHECK(read == +Channel::Red);

  if (failures == 0) std::puts("ok");
  return failures;
}
