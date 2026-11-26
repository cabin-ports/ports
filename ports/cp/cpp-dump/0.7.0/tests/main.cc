#include <cpp-dump.hpp>

#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

// cpp_dump writes to std::clog; capture what one call prints.
template <typename F>
std::string capture(F f) {
  std::ostringstream ss;
  std::streambuf *old = std::clog.rdbuf(ss.rdbuf());
  f();
  std::clog.rdbuf(old);
  return ss.str();
}

void check(const std::string &got, const std::string &want, const char *what) {
  if (got != want) {
    std::fprintf(stderr, "FAIL: %s\n  got:  %s\n  want: %s\n", what, got.c_str(),
                 want.c_str());
    failures++;
  }
}

}  // namespace

int main() {
  CPP_DUMP_SET_OPTION(es_style, cpp_dump::types::es_style_t::no_es);

  int my_int = 15;
  std::map<int, int> my_map{{2, 6}, {4, 6}, {5, 3}};
  check(capture([&] { cpp_dump(my_int, my_map); }),
        "[dump] my_int => 15, my_map => { 2: 6, 4: 6, 5: 3 }\n", "int and map");

  std::set<char> my_set{'p', 'A', 'l', 'e'};
  std::vector<int> my_vector{3, 5, 8, 9, 7};
  check(capture([&] { cpp_dump(my_set, my_vector); }),
        "[dump] my_set => { 'A', 'e', 'l', 'p' }, my_vector => [ 3, 5, 8, 9, 7 ]\n",
        "set and vector");

  std::string str = "This is a string.";
  check(capture([&] { cpp_dump(false, 3.14, str, 'a', nullptr); }),
        "[dump] false => false, 3.14 => 3.140000, str => \"This is a string.\", 'a' => 'a', "
        "nullptr => nullptr\n",
        "basic types");

  std::tuple<int, std::string> my_tuple{1, "one"};
  check(capture([&] { cpp_dump(my_tuple); }),
        "[dump] my_tuple => ( 1, \"one\" )\n", "tuple");

  // front(n) limits a container to its first n elements.
  std::vector<int> big(40, 7);
  check(capture([&] { cpp_dump(cpp_dump::front(2) << big); }),
        "[dump] cpp_dump::front(2) << big => [ 7, 7, ... ]\n", "front manipulator");

  if (failures != 0) {
    return 1;
  }
  std::puts("ok");
  return 0;
}
