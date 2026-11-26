#include <flags/flags.hpp>

#include <cstdio>
#include <vector>

enum class E : unsigned { A = 1, B = 2, C = 4, D = 8 };
ALLOW_FLAGS_FOR_ENUM(E)

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                      \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main() {
  auto m = E::A | E::C;
  CHECK(m.underlying_value() == 5u);
  CHECK(m.size() == 2);
  CHECK(static_cast<bool>(m & E::C));
  CHECK(!(m & E::B));
  CHECK((m & E::A).underlying_value() == 1u);
  CHECK((E::A ^ E::A).underlying_value() == 0u);
  CHECK((m ^ E::B).underlying_value() == 7u);

  // Set flags come out in ascending bit order.
  std::vector<E> seen;
  for (E e : (E::D | E::A | E::C))
    seen.push_back(e);
  CHECK(seen.size() == 3);
  CHECK(seen.size() == 3 && seen[0] == E::A && seen[1] == E::C && seen[2] == E::D);

  // erase clears the bit; it must not toggle it back on.
  CHECK(m.erase(E::A) == 1);
  CHECK(m.underlying_value() == 4u);
  CHECK(m.erase(E::A) == 0);
  CHECK(m.underlying_value() == 4u);

  m.insert(E::D);
  CHECK(m.count(E::D) == 1);
  CHECK(m.count(E::B) == 0);
  CHECK(m.underlying_value() == 12u);

  flags::flags<E> z{flags::empty};
  CHECK(z.empty());
  CHECK(!z);
  CHECK(z.size() == 0);

  std::printf(failures ? "enum-flags: FAILED\n" : "enum-flags: ok\n");
  return failures != 0;
}
