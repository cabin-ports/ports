#include <daw/daw_fnv1a_hash.h>
#include <daw/daw_string_view.h>

#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

// 64-bit FNV-1a of "hello", computed independently.
static_assert(daw::fnv1a_hash("hello") == 0xa430d84680aabd0bULL,
              "fnv1a_hash(\"hello\")");

int main() {
  daw::string_view sv = "a,b,,c";
  daw::string_view const a = sv.pop_front_until(',');
  daw::string_view const b = sv.pop_front_until(',');
  daw::string_view const empty = sv.pop_front_until(',');
  check(a == "a" && b == "b" && empty.empty(), "tokens a, b and empty");
  check(sv == "c" && sv.size() == 1, "\"c\" remains");
  daw::string_view const c = sv.pop_front_until(',');
  check(c == "c" && sv.empty(), "last token c, nothing left");

  if (failures != 0) {
    return 1;
  }
  std::printf("daw-header-libraries 2.136.0 C++ ok\n");
  return 0;
}
