#include <a4z/astr.hpp>
#include <a4z/filename.hpp>
#include <a4z/typename.hpp>

#include <cstdio>
#include <string_view>

namespace ns {
  struct Widget {};
}  // namespace ns

namespace {
  int failures = 0;

  void check(bool ok, const char* what) {
    if (!ok) {
      std::printf("FAIL: %s\n", what);
      ++failures;
    }
  }
}  // namespace

int main() {
  constexpr char abc[] = "0123456789";
  constexpr a4z::astr a10(abc);
  static_assert(a10.size() == 10);
  static_assert(a4z::equal("0123456789", a10.c_str()));
  check(std::string_view(a10) == "0123456789", "astr converts to string_view");

  constexpr auto last5 = a10.last_n<5>();
  constexpr auto first5 = a10.first_n<5>();
  static_assert(last5.size() == 5 && a4z::equal("56789", last5.c_str()));
  static_assert(first5.size() == 5 && a4z::equal("01234", first5.c_str()));
  check(std::string_view(last5) == "56789", "last_n<5>");
  check(std::string_view(first5) == "01234", "first_n<5>");

  constexpr char path[] = "/a/b/c";
  static_assert(a4z::find_nth_r_occurrence(path, '/', 1) == 4);
  static_assert(a4z::find_nth_r_occurrence(path, '/', 2) == 2);
  static_assert(a4z::find_nth_r_occurrence("ab/c", '/', 2) == a4z::npos);
  static_assert(a4z::cstr_len(path) == 6);
  static_assert(a4z::first_c_in('/', path) == path);
  static_assert(a4z::last_c_in('/', path) == path + 4);
  static_assert(a4z::first_c_in('x', path) == nullptr);
  static_assert(a4z::first_not('/', path) == path + 1);
  static_assert(a4z::last_char_in('/', path) == path + 4);
  static_assert(!a4z::equal("abc", "abd"));

  // The macro keeps the last directory and the file name of __FILE__.
  constexpr auto fn = a4z_file_name();
  check(std::string_view(fn).size() > 8, "file name keeps a directory");
  check(std::string_view(fn.last_n<7>()) == "main.cc", "file name ends with main.cc");
  check(fn.c_str()[fn.size() - 8] == a4z::slash, "directory and file are separated");

  check(std::string_view(a4z::type_name<int>()) == "int", "type_name<int>");
  check(std::string_view(a4z::type_name<ns::Widget>()) == "ns::Widget",
        "type_name of a namespaced struct");

  if (failures == 0) std::puts("ok");
  return failures != 0;
}
