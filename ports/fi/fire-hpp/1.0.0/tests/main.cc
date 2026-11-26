// FIRE defines main and takes argv from the process, so it is renamed to
// fire_entry here and called with chosen argument vectors.
#define main fire_entry
#include <fire-hpp/fire.hpp>

#include <cstdio>
#include <string>

namespace {

int opt_value;
bool opt_set;
int def_value;
double ratio_value;
bool flag_value;
std::string name_value;
int first_pos;

int fired_main(fire::optional<int> opt = fire::arg("--optional"),
               int def = fire::arg("--default", 7),
               double ratio = fire::arg({"-r", "--ratio"}, 0.5),
               bool flag = fire::arg({"-f", "--flag"}),
               std::string name = fire::arg("--name", "none"),
               int pos = fire::arg(0)) {
  opt_set = opt.has_value();
  opt_value = opt_set ? opt.value() : -1;
  def_value = def;
  ratio_value = ratio;
  flag_value = flag;
  name_value = name;
  first_pos = pos;
  return 0;
}

}  // namespace

FIRE(fired_main)
#undef main

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

template <int N>
int run(const char *(&argv)[N]) {
  return fire_entry(N, argv);
}

}  // namespace

int main() {
  {
    const char *argv[] = {"prog", "4"};
    check(run(argv) == 0, "defaults: exit status");
    check(!opt_set, "defaults: optional is empty");
    check(def_value == 7, "defaults: default value");
    check(ratio_value == 0.5, "defaults: ratio");
    check(!flag_value, "defaults: flag off");
    check(name_value == "none", "defaults: string default");
    check(first_pos == 4, "defaults: positional");
  }
  {
    const char *argv[] = {"prog", "--optional=5", "--default", "9", "-r",
                          "2.5",  "--flag",       "--name=fire", "12"};
    check(run(argv) == 0, "given: exit status");
    check(opt_set && opt_value == 5, "given: optional value");
    check(def_value == 9, "given: default overridden");
    check(ratio_value == 2.5, "given: short name with float");
    check(flag_value, "given: flag on");
    check(name_value == "fire", "given: string value");
    check(first_pos == 12, "given: positional");
  }
  return failures == 0 ? 0 : 1;
}
