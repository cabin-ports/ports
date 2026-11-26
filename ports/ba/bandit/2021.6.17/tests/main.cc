#include <bandit/bandit.h>

#include <exception>
#include <stdexcept>

using namespace snowhouse;
using namespace bandit;

namespace {
int outer_before = 0, inner_before = 0, its_run = 0, throws_checked = 0;
}

go_bandit([]() {
  describe("outer", []() {
    int value = 0;
    before_each([&]() {
      ++outer_before;
      value = 40;
    });
    it("sees a fresh value", [&]() {
      ++its_run;
      AssertThat(value, Equals(40));
      value = 99;
    });
    it("is unaffected by the previous it", [&]() {
      ++its_run;
      AssertThat(value, Equals(40));
      AssertThat(value, IsGreaterThan(39));
      AssertThat(std::string("bandit"), StartsWith("ban"));
    });
    it("catches thrown exceptions", [&]() {
      ++its_run;
      AssertThrows(std::runtime_error, throw std::runtime_error("x"));
      AssertThat(LastException<std::runtime_error>().what(), Equals(std::string("x")));
      ++throws_checked;
    });
    describe("nested", [&]() {
      before_each([&]() {
        ++inner_before;
        value += 2;
      });
      it("runs outer then inner before_each", [&]() {
        ++its_run;
        AssertThat(value, Equals(42));
      });
    });
  });
});

int main() {
  char arg0[] = "t", arg1[] = "--colorizer=off", arg2[] = "--reporter=singleline";
  char* argv[] = {arg0, arg1, arg2, nullptr};

  if (bandit::run(3, argv) != 0) return 1;
  if (outer_before != 4 || inner_before != 1 || its_run != 4 || throws_checked != 1) return 2;

  // A failing it makes the run return nonzero; the passing one beside it still runs.
  bandit::detail::specs().clear();
  int passing = 0;
  bandit::detail::specs().push_back([&]() {
    describe("failing", [&]() {
      it("fails", []() { AssertThat(1, Equals(2)); });
      it("passes", [&]() { ++passing; });
    });
  });
  if (bandit::run(3, argv) != 1) return 3;
  return passing == 1 ? 0 : 4;
}
