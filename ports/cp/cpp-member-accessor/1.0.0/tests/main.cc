#include <accessor/accessor.hpp>

#include <cstdio>

class Widget {
  int hidden = 42;
  int add(int a, int b) const { return a + b + hidden; }
  void scale(int factor) { hidden *= factor; }
  void bump(int& x) const { x += hidden; }
};

MEMBER_ACCESSOR(WidgetHidden, Widget, hidden, int)
CONST_FUNCTION_ACCESSOR(WidgetAdd, Widget, add, int, int, int)
FUNCTION_ACCESSOR(WidgetScale, Widget, scale, void, int)
CONST_FUNCTION_ACCESSOR(WidgetBump, Widget, bump, void, int&)

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

int main() {
  Widget w;
  check(accessor::accessMember<WidgetHidden>(w).get() == 42, "hidden == 42");
  check(accessor::callFunction<WidgetAdd>(w, 1, 2) == 45, "add(1, 2) == 45");

  accessor::accessMember<WidgetHidden>(w).get() = 7;
  check(accessor::callFunction<WidgetAdd>(w, 1, 2) == 10, "add after write");

  accessor::callFunction<WidgetScale>(w, 3);
  check(accessor::accessMember<WidgetHidden>(w).get() == 21, "scale(3)");

  int v = 1;
  accessor::callFunction<WidgetBump>(w, v);
  check(v == 22, "bump forwards an lvalue reference");

  if (failures != 0) {
    return 1;
  }
  std::printf("cpp-member-accessor 1.0.0 C++ ok\n");
  return 0;
}
