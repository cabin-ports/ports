#include <nonstd/status_value.hpp>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

using nonstd::status_value;

status_value<std::string, int> parse_digit(char c) {
  if (c >= '0' && c <= '9')
    return {"ok", c - '0'};
  return {"not a digit"};
}

// Counts live objects, so the contained value's construction and destruction
// can be checked.
struct Tracked {
  static int live;
  int value;
  explicit Tracked(int v) : value(v) { ++live; }
  Tracked(Tracked const &other) : value(other.value) { ++live; }
  Tracked(Tracked &&other) : value(other.value) {
    other.value = -1;
    ++live;
  }
  ~Tracked() { --live; }
};
int Tracked::live = 0;

void check_observers() {
  status_value<std::string, int> ok = parse_digit('7');
  CHECK(ok.has_value() && static_cast<bool>(ok));
  CHECK(*ok == 7 && ok.value() == 7);
  CHECK(ok.status() == "ok");
  status_value<std::string, int> const &view = ok;
  CHECK(*view == 7 && view.value() == 7);

  status_value<std::string, int> bad = parse_digit('x');
  CHECK(!bad.has_value() && !static_cast<bool>(bad));
  CHECK(bad.status() == "not a digit");
}

void check_bad_access() {
  status_value<std::string, int> bad = parse_digit('x');
  bool threw = false;
  try {
    (void)bad.value();
  } catch (nonstd::bad_status_value_access<std::string> const &e) {
    threw = true;
    CHECK(e.status() == "not a digit");
    CHECK(std::string(e.what()) == "status_value: bad status_value access");
  }
  CHECK(threw);

  status_value<std::string, Tracked> none{"empty"};
  threw = false;
  try {
    (void)none->value;
  } catch (std::logic_error const &) {
    threw = true;
  }
  CHECK(threw);

  // Const access throws bad_status_value_access<S const>, as upstream's
  // test/status_value.t.cpp:195 expects.
  status_value<std::string, Tracked> const &cnone = none;
  threw = false;
  try {
    (void)cnone.value();
  } catch (nonstd::bad_status_value_access<std::string const> const &e) {
    threw = e.status() == "empty";
  }
  CHECK(threw);
  threw = false;
  try {
    (void)cnone->value;
  } catch (std::logic_error const &) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    (void)std::move(bad).value();
  } catch (nonstd::bad_status_value_access<std::string> const &e) {
    threw = e.status() == "not a digit";
  }
  CHECK(threw);
}

void check_lifetime() {
  {
    status_value<int, Tracked> held{0, Tracked{5}};
    CHECK(Tracked::live == 1 && held->value == 5);
    status_value<int, Tracked> moved{std::move(held)};
    CHECK(moved.has_value() && moved->value == 5);
    CHECK(!held.has_value() && held.status() == 0);
    CHECK(Tracked::live == 1);
    status_value<int, Tracked> none{3};
    CHECK(!none.has_value() && Tracked::live == 1);
    status_value<int, Tracked> none2{std::move(none)};
    CHECK(!none2.has_value() && none2.status() == 3 && Tracked::live == 1);
    Tracked source{4};
    status_value<int, Tracked> copied{1, source};
    CHECK(Tracked::live == 3 && copied->value == 4 && source.value == 4);
    // Rvalue value() and operator* move the value out.
    Tracked out = std::move(copied).value();
    CHECK(out.value == 4 && copied->value == -1);
    status_value<int, Tracked> again{2, Tracked{6}};
    Tracked out2 = *std::move(again);
    CHECK(out2.value == 6 && again->value == -1);
  }
  CHECK(Tracked::live == 0);
}

void check_move_only_and_rvalues() {
  status_value<std::string, std::unique_ptr<int>> owner{
      "ok", std::unique_ptr<int>(new int(9))};
  CHECK(**owner == 9);
  status_value<std::string, std::unique_ptr<int>> taken = std::move(owner);
  CHECK(!owner.has_value() && taken.has_value() && **taken == 9);
  CHECK(taken.status() == "ok");

  status_value<int, std::string> text{0, std::string("payload")};
  std::string out = std::move(text).value();
  CHECK(out == "payload");
}

} // namespace

int main() {
  CHECK(status_value_MAJOR == 1 && status_value_MINOR == 1 &&
        status_value_PATCH == 0);
  check_observers();
  check_bad_access();
  check_lifetime();
  check_move_only_and_rvalues();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("status-value-lite ok");
  return 0;
}
