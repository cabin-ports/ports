// The schema-independent API; tests/tick exercises generated code.
#include <cstdio>

#include <sbepp/sbepp.hpp>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  char buf[4] = {};
  sbepp::cursor<char> c;
  check(c.pointer() == nullptr, "a default cursor holds nullptr");
  c.pointer() = buf + 2;
  sbepp::cursor<const char> cc = c;
  check(cc.pointer() == buf + 2, "a cursor converts to const");

  check(static_cast<int>(sbepp::eos_null::none) == 0 &&
            static_cast<int>(sbepp::eos_null::single) == 1 &&
            static_cast<int>(sbepp::eos_null::all) == 2,
        "eos_null modes");

  if (failures != 0) {
    return 1;
  }
  std::puts("sbepp: ok");
  return 0;
}
