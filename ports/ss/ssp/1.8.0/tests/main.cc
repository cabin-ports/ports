// C++17 consumer: the README's buffer-mode example (README.md:241-246),
// quoting (README.md:284-292), and ss::converter (README.md:646-649).
#include <ss/parser.hpp>

#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);              \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  {
    std::string buf = "James Bailey,65,2.5\nBrian S. Wolfe,40,1.9\n";
    ss::parser<ss::throw_on_error> p{buf.c_str(), buf.size()};
    std::vector<std::tuple<std::string, int, double>> rows;
    for (const auto& row : p.iterate<std::string, int, double>()) {
      rows.push_back(row);
    }
    CHECK(rows.size() == 2);
    if (rows.size() == 2) {
      CHECK(rows[0] == std::make_tuple(std::string("James Bailey"), 65, 2.5));
      CHECK(rows[1] ==
            std::make_tuple(std::string("Brian S. Wolfe"), 40, 1.9));
    }
  }
  {
    std::string buf = "abc,1\n";
    ss::parser<ss::throw_on_error> p{buf.c_str(), buf.size()};
    bool threw = false;
    try {
      p.get_next<int, int>();
    } catch (const ss::exception&) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    std::string buf = "\"James \"\"Bailey\"\"\",65\n";
    ss::parser<ss::quote<'"'>, ss::throw_on_error> p{buf.c_str(),
                                                     buf.size()};
    auto [name, age] = p.get_next<std::string, int>();
    CHECK(name == "James \"Bailey\"");
    CHECK(age == 65);
  }
  {
    ss::converter c;
    auto [x, y, z] = c.convert<int, double, char>("10::2.2::3", "::");
    CHECK(c.valid());
    CHECK(x == 10);
    CHECK(y == 2.2);
    CHECK(z == '3');
    c.convert<int>("abc");
    CHECK(!c.valid());
  }
  return failures == 0 ? 0 : 1;
}
