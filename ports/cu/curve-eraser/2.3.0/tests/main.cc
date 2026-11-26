#include <eraser/erased.hpp>
#include <eraser/experimental/deduce.hpp>
#include <eraser/storage/shared.hpp>

#include <concepts>
#include <cstdio>
#include <utility>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

struct scaler {
  int k;
  int weight() const { return k; }
  int apply(int x) const { return x * k; }
};

struct shifter {
  int k;
  int weight() const { return -k; }
  int apply(int x) const { return x + k; }
};

enum class methods { weight, apply };

using iface = eraser::interface<
    eraser::method<methods::weight, [](auto& self) { return self.weight(); }, int()>,
    eraser::method<methods::apply, [](auto& self, int x) { return self.apply(x); },
                   int(int)>>;

struct deduced {
  int weight() {
    return eraser::experimental::call<int>(
        [](auto& self) { return self.weight(); }, this);
  }
  int apply(int x) {
    return eraser::experimental::call<int>(
        [](auto& self, int x) { return self.apply(x); }, this, x);
  }
};

int main() {
  using eraser::erased;
  using eraser::make_erased;
  using eraser::experimental::deduce;

  auto e1 = make_erased<iface, scaler, eraser::storage::shared>(7);
  erased<iface> e2{shifter{5}};

  check(std::copyable<decltype(e1)>, "shared storage is copyable");
  check(!std::copyable<decltype(e2)>, "unique storage is not copyable");
  check(std::movable<decltype(e2)>, "unique storage is movable");

  check(e1.invoke<methods::weight>() == 7, "e1 weight");
  check(e2.invoke<methods::weight>() == -5, "e2 weight");
  check(e1.invoke<methods::apply>(6) == 42, "e1 apply");
  check(e2.invoke<methods::apply>(6) == 11, "e2 apply");

  auto copy = e1;
  check(copy.as<scaler>().value() == e1.as<scaler>().value(),
        "copy shares the object");
  check(copy.invoke<methods::apply>(3) == 21, "copy apply");

  auto moved = std::move(e2);
  check(moved.invoke<methods::apply>(1) == 6, "moved apply");

  check(e1.as<scaler>().has_value(), "as<scaler>");
  check(!e1.as<shifter>().has_value(), "as<wrong type> is empty");
  check(moved.as<shifter>().value()->k == 5, "as<shifter>");
  check(!moved.as<scaler>().has_value(), "as<wrong type> is empty (2)");

  auto d1 = make_erased<deduce<deduced>, scaler, eraser::storage::shared>(7);
  erased<deduce<deduced>> d2{shifter{5}};

  check(std::copyable<decltype(d1)>, "deduce shared copyable");
  check(!std::copyable<decltype(d2)>, "deduce unique not copyable");
  check(d1.weight() == 7 && d2.weight() == -5, "deduce weight");
  check(d1.apply(6) == 42 && d2.apply(6) == 11, "deduce apply");
  check(d1.as<scaler>().has_value() && !d1.as<shifter>().has_value(),
        "deduce as");

  if (failures == 0) std::printf("ok\n");
  return failures;
}
