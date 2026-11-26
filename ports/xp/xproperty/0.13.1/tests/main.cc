#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include <xproperty/xjson.hpp>
#include <xproperty/xobserved.hpp>
#include <xproperty/xproperty.hpp>

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

struct Widget : xp::xobserved {
  XPROPERTY(int, Widget, count, 3);
  XPROPERTY(std::string, Widget, label, "idle", [](std::string& v) {
    if (v.empty()) throw std::invalid_argument("empty label");
    v += "!";
  });
  XPROPERTY(double, Widget, ratio);
};

}  // namespace

int main() {
  Widget w;

  // Defaults, set and get.
  CHECK(w.count == 3);
  CHECK(w.ratio == 0.0);
  CHECK(w.label() == "idle");
  CHECK(std::string(w.count.name()) == "count");
  w.count = 42;
  CHECK(w.count() == 42);
  w.ratio = 2.5;
  CHECK(static_cast<double>(w.ratio) == 2.5);

  // Constructor validator transforms and rejects; a rejected value leaves
  // the property unchanged.
  w.label = std::string("busy");
  CHECK(w.label() == "busy!");
  bool threw = false;
  try {
    w.label = std::string();
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(w.label() == "busy!");

  // Dynamic validator.  Callbacks are keyed by name pointer, so pass the
  // property's own name() rather than a separate literal.
  w.validate<Widget, int>(w.count.name(), [](Widget&, int& v) {
    if (v < 0) throw std::runtime_error("negative");
    if (v > 100) v = 100;
  });
  w.count = 500;
  CHECK(w.count() == 100);
  threw = false;
  try {
    w.count = -1;
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(w.count() == 100);
  w.unvalidate(w.count.name());
  w.count = -7;
  CHECK(w.count() == -7);

  // Observers see the new value and fire once per assignment, in order.
  std::vector<int> seen;
  w.observe<Widget>(w.count.name(), [&seen](Widget& o) { seen.push_back(o.count()); });
  w.observe<Widget>(w.count.name(),
                    [&seen](Widget& o) { seen.push_back(o.count() * 10); });
  w.count = 1;
  w.count = 2;
  CHECK((seen == std::vector<int>{1, 10, 2, 20}));
  w.ratio = 9.0;  // other property: no observer
  CHECK(seen.size() == 4);
  w.unobserve(w.count.name());
  w.count = 3;
  CHECK(seen.size() == 4);

  // JSON round trip through xjson.hpp.
  nlohmann::json j;
  xp::to_json(j, w.count);
  CHECK(j == nlohmann::json(3));
  xp::to_json(j, w.label);
  CHECK(j == nlohmann::json("busy!"));
  xp::from_json(nlohmann::json(77), w.count);
  CHECK(w.count() == 77);
  xp::from_json(nlohmann::json(0.125), w.ratio);
  CHECK(w.ratio() == 0.125);

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
