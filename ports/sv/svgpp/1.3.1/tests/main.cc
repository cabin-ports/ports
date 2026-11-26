#include <svgpp/svgpp.hpp>

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

using namespace svgpp;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

struct PathContext {
  std::ostringstream log;

  template <class Abs>
  void path_move_to(double x, double y, Abs) {
    log << (Abs::is_absolute ? "M" : "m") << x << ',' << y << ' ';
  }
  template <class Abs>
  void path_line_to(double x, double y, Abs) {
    log << (Abs::is_absolute ? "L" : "l") << x << ',' << y << ' ';
  }
  template <class Abs>
  void path_line_to_ortho(double c, bool horizontal, Abs) {
    log << (Abs::is_absolute ? (horizontal ? "H" : "V") : (horizontal ? "h" : "v")) << c << ' ';
  }
  template <class Abs>
  void path_cubic_bezier_to(double x1, double y1, double x2, double y2, double x, double y, Abs) {
    log << (Abs::is_absolute ? "C" : "c") << x1 << ',' << y1 << ',' << x2 << ',' << y2 << ',' << x
        << ',' << y << ' ';
  }
  template <class Abs>
  void path_cubic_bezier_to(double x2, double y2, double x, double y, Abs) {
    log << (Abs::is_absolute ? "S" : "s") << x2 << ',' << y2 << ',' << x << ',' << y << ' ';
  }
  template <class Abs>
  void path_quadratic_bezier_to(double x1, double y1, double x, double y, Abs) {
    log << (Abs::is_absolute ? "Q" : "q") << x1 << ',' << y1 << ',' << x << ',' << y << ' ';
  }
  template <class Abs>
  void path_quadratic_bezier_to(double x, double y, Abs) {
    log << (Abs::is_absolute ? "T" : "t") << x << ',' << y << ' ';
  }
  template <class Abs>
  void path_elliptical_arc_to(double rx, double ry, double rot, bool large, bool sweep, double x,
                              double y, Abs) {
    log << (Abs::is_absolute ? "A" : "a") << rx << ',' << ry << ',' << rot << ',' << large << ','
        << sweep << ',' << x << ',' << y << ' ';
  }
  void path_close_subpath() { log << "Z "; }
  void path_exit() { log << "end"; }
};

struct ValueContext {
  double width = -1;
  int color = -1;
  void set(tag::attribute::width, double v) { width = v; }
  void set(tag::attribute::color, int v) { color = v; }
};

struct TransformContext {
  boost::array<double, 6> m;
  int calls = 0;
  void transform_matrix(const boost::array<double, 6>& matrix) {
    m = matrix;
    ++calls;
  }
};

template <class Policy>
std::string parse_path(const std::string& d) {
  PathContext ctx;
  value_parser<tag::type::path_data, path_policy<Policy> >::parse(tag::attribute::d(), ctx, d,
                                                                  tag::source::attribute());
  return ctx.log.str();
}

double parse_width(const std::string& s) {
  ValueContext ctx;
  value_parser<tag::type::length>::parse(tag::attribute::width(), ctx, s, tag::source::attribute());
  return ctx.width;
}

int parse_color(const std::string& s) {
  ValueContext ctx;
  value_parser<tag::type::color>::parse(tag::attribute::color(), ctx, s, tag::source::attribute());
  return ctx.color;
}

bool transform_matches(const std::string& s, const double (&expected)[6]) {
  TransformContext ctx;
  value_parser<tag::type::transform_list>::parse(tag::attribute::transform(), ctx, s,
                                                 tag::source::attribute());
  if (ctx.calls != 1) return false;
  for (int i = 0; i < 6; ++i)
    if (!near(ctx.m[i], expected[i])) return false;
  return true;
}

}  // namespace

int main() {
  // Path data: raw keeps every command as written.
  check(parse_path<policy::path::raw>("M10 20 L30 40 h5 V7 c1,2 3,4 5,6 z") ==
            "M10,20 L30,40 h5 V7 c1,2,3,4,5,6 Z end",
        "raw path data");
  // The default policy turns relative commands and h/v into absolute lines.
  check(parse_path<policy::path::no_shorthands>("m10 20 l5 5 h10 v-5 z") ==
            "M10,20 L15,25 L25,25 L25,20 Z end",
        "default path policy");

  // Transform list: translate(10,20) then scale(2) is one matrix.
  const double ts[6] = {2, 0, 0, 2, 10, 20};
  check(transform_matches("translate(10,20) scale(2)", ts), "translate then scale");
  const double rot[6] = {0, 1, -1, 0, 0, 0};
  check(transform_matches("rotate(90)", rot), "rotate 90");

  // Color
  check(parse_color("#ff8000") == 0xff8000, "hex color");
  check(parse_color("rgb(0%, 50%, 100%)") == 0x007fff, "percent rgb color");
  check(parse_color("rgb(1, 2, 3)") == 0x010203, "integer rgb color");

  // Length: 96 px per inch by default.
  check(near(parse_width("1in"), 96), "1in");
  check(near(parse_width("12pt"), 16), "12pt");
  check(near(parse_width("10"), 10), "unitless");
  check(near(parse_width("2.54cm"), 96), "2.54cm");

  // A malformed value reaches the default error policy, which throws.
  bool threw = false;
  try {
    parse_width("1 inch");
  } catch (const invalid_value_error<char>&) {
    threw = true;
  }
  check(threw, "invalid length throws");

  return failures == 0 ? 0 : 1;
}
