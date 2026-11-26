#include <visit_struct/visit_struct.hpp>
#include <visit_struct/visit_struct_intrusive.hpp>

#include <cstdio>
#include <string>
#include <type_traits>

struct point {
  int x;
  double y;
  std::string name;
};
VISITABLE_STRUCT(point, x, y, name);

struct pair_of_ints {
  int a;
  int b;
};
VISITABLE_STRUCT(pair_of_ints, a, b);

struct intrusive {
  BEGIN_VISITABLES(intrusive);
  VISITABLE(int, a);
  VISITABLE(int, b);
  VISITABLE(double, c);
  END_VISITABLES;
};

static_assert(visit_struct::field_count<point>() == 3, "");
static_assert(visit_struct::field_count<intrusive>() == 3, "");
static_assert(std::is_same<visit_struct::type_at<1, point>, double>::value, "");

namespace {

struct collector {
  std::string names;
  double sum = 0;
  void operator()(const char* n, const std::string& v) { names += n; names += "=" + v + ";"; }
  template <typename T>
  void operator()(const char* n, const T& v) { names += n; names += ";"; sum += v; }
};

struct summer {
  int total = 0;
  void operator()(const char*, int l, int r) { total += l * r; }
};

int fail(const char* what) {
  std::fprintf(stderr, "visit_struct: %s\n", what);
  return 1;
}

}  // namespace

int main() {
  point p{1, 2.5, "abc"};
  collector c;
  visit_struct::for_each(p, c);
  if (c.names != "x;y;name=abc;") return fail("for_each names");
  if (c.sum != 3.5) return fail("for_each values");

  if (visit_struct::field_count(p) != 3) return fail("field_count");
  if (visit_struct::get<0>(p) != 1) return fail("get<0>");
  if (visit_struct::get<1>(p) != 2.5) return fail("get<1>");
  if (visit_struct::get<2>(p) != "abc") return fail("get<2>");
  visit_struct::get<0>(p) = 7;
  if (p.x != 7) return fail("get is an lvalue reference");
  if (std::string(visit_struct::get_name<2, point>()) != "name") return fail("get_name<2>");
  if (std::string(visit_struct::get_name<point>()) != "point") return fail("struct name");

  pair_of_ints l{2, 3}, r{4, 5};
  summer s;
  visit_struct::apply_visitor(s, l, r);
  if (s.total != 2 * 4 + 3 * 5) return fail("apply_visitor over two instances");

  intrusive i;
  i.a = 4;
  i.b = 5;
  i.c = 0.5;
  collector ic;
  visit_struct::for_each(i, ic);
  if (ic.names != "a;b;c;") return fail("intrusive names");
  if (ic.sum != 9.5) return fail("intrusive values");
  if (visit_struct::get<1>(i) != 5) return fail("intrusive get");
  if (std::string(visit_struct::get_name<2>(i)) != "c") return fail("intrusive get_name");

  if (std::string(VISIT_STRUCT_VERSION_STRING) != "1.2.0") return fail("version");
  return 0;
}
