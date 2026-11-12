// ExprTk from C++11: expressions compiled against a symbol table and
// evaluated, with constants, variables, vectors, strings, control flow, a
// user-defined function, a function compositor, the vecops package, the
// helpers, and the parser's diagnostics for invalid input.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <exprtk.hpp>

namespace {

typedef exprtk::symbol_table<double> symbol_table_t;
typedef exprtk::expression<double> expression_t;
typedef exprtk::parser<double> parser_t;
typedef exprtk::function_compositor<double> compositor_t;
typedef compositor_t::function function_t;

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

bool nearly_equal(double actual, double expected, double tolerance) {
  return std::fabs(actual - expected) <=
         tolerance * (1.0 + std::fabs(expected));
}

bool compile(symbol_table_t& symbols, const std::string& text,
             expression_t& expression) {
  expression.register_symbol_table(symbols);
  parser_t parser;
  if (parser.compile(text, expression)) {
    return true;
  }
  for (std::size_t i = 0; i < parser.error_count(); ++i) {
    const exprtk::parser_error::type error = parser.get_error(i);
    std::fprintf(stderr, "'%s': %s at %d: %s\n", text.c_str(),
                 exprtk::parser_error::to_str(error.mode).c_str(),
                 static_cast<int>(error.token.position),
                 error.diagnostic.c_str());
  }
  return false;
}

// The value of text, or NaN (and a failed check) if it does not compile.
double value_of(symbol_table_t& symbols, const std::string& text, int line) {
  expression_t expression;
  if (!compile(symbols, text, expression)) {
    check(false, line, text.c_str());
    return std::sqrt(-1.0);
  }
  return expression.value();
}

#define VALUE(symbols, text) value_of((symbols), (text), __LINE__)

struct hypot3 : public exprtk::ifunction<double> {
  hypot3() : exprtk::ifunction<double>(3) {}
  double operator()(const double& a, const double& b, const double& c) {
    return std::sqrt(a * a + b * b + c * c);
  }
};

void scalars() {
  double x = 0.0;
  double y = 0.0;
  symbol_table_t symbols;
  symbols.add_constants();
  symbols.add_variable("x", x);
  symbols.add_variable("y", y);

  // Implicit multiplication; the compiled expression reads x by reference.
  expression_t poly;
  CHECK(compile(symbols, "25x^5 - 35x^4 - 15x^3 + 40x^2 - 15x + 1", poly));
  const double points[] = {0.0, 0.25, 0.5, 1.0, 1.75};
  for (std::size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
    x = points[i];
    const double horner =
        ((((25 * x - 35) * x - 15) * x + 40) * x - 15) * x + 1;
    CHECK(nearly_equal(poly.value(), horner, 1e-12));
  }

  x = 2.0;
  CHECK(nearly_equal(VALUE(symbols, "sin(pi / 6)"), 0.5, 1e-15));
  CHECK(VALUE(symbols, "clamp(-1, 5, 1)") == 1.0);
  CHECK(VALUE(symbols, "avg(1, 2, 3, 6)") == 3.0);
  CHECK(VALUE(symbols, "if (x > 1, 10, 20)") == 10.0);
  CHECK(VALUE(symbols, "x < 1 ? 10 : 20") == 20.0);
  CHECK(VALUE(symbols, "inrange(-2, x, 2)") == 1.0);
  CHECK(VALUE(symbols, "roundn(1.23456, 2)") == 1.23);
  CHECK(VALUE(symbols,
              "(2sin(x)cos(2y)7 + 1) == (2 * sin(x) * cos(2*y) * 7 + 1)") ==
        1.0);

  CHECK(VALUE(symbols, "var a := 3; var b := a * 2; b + 1") == 7.0);
  CHECK(VALUE(symbols, "y := x * 2.5") == 5.0);
  CHECK(y == 5.0);
  CHECK(VALUE(symbols,
             "var s := 0; for (var i := 0; i < 10; i += 1) { s += i; }; s")
        == 45.0);
  CHECK(VALUE(symbols, "var n := 0; while (n < 5) { n += 1; }; n") == 5.0);
  CHECK(VALUE(symbols,
              "switch { case x > 5 : 1; case x > 1 : 2; default : 3; }") ==
        2.0);

  hypot3 h;
  CHECK(symbols.add_function("hypot3", h));
  CHECK(VALUE(symbols, "hypot3(1, 2, 2)") == 3.0);
}

void vectors_and_strings() {
  std::vector<double> v;
  v.push_back(4.0);
  v.push_back(1.0);
  v.push_back(3.0);
  v.push_back(2.0);
  std::string s = "abc";
  symbol_table_t symbols;
  symbols.add_vector("v", v);
  symbols.add_stringvar("s", s);
  exprtk::rtl::vecops::package<double> vecops;
  CHECK(symbols.add_package(vecops));

  CHECK(VALUE(symbols, "sum(v)") == 10.0);
  CHECK(VALUE(symbols, "avg(v)") == 2.5);
  CHECK(VALUE(symbols, "v[0] + v[3]") == 6.0);
  CHECK(VALUE(symbols, "v[]") == 4.0);
  CHECK(VALUE(symbols, "sort(v)") == 1.0);
  CHECK(v[0] == 1.0 && v[1] == 2.0 && v[2] == 3.0 && v[3] == 4.0);
  CHECK(VALUE(symbols, "v := v * 2; max(v)") == 8.0);
  CHECK(v[3] == 8.0);

  CHECK(VALUE(symbols, "s == 'abc'") == 1.0);
  CHECK(VALUE(symbols, "s like 'a*c'") == 1.0);
  VALUE(symbols, "s := s + 'def'");
  CHECK(s == "abcdef");
  CHECK(VALUE(symbols, "s[]") == 6.0);
}

void compositor() {
  compositor_t compositor;
  symbol_table_t& symbols = compositor.symbol_table();
  CHECK(compositor.add(function_t("f").var("x").expression("x * x + 1")));
  CHECK(compositor.add(
      function_t("g").vars("x", "y").expression("f(x) + f(y)")));
  CHECK(VALUE(symbols, "g(2, 3)") == 15.0);
}

void helpers_and_errors() {
  double result = 0.0;
  CHECK(exprtk::compute("2 * (3 + 4)", result) && result == 14.0);

  std::vector<std::string> names;
  CHECK(exprtk::collect_variables("a + b * c - a", names));
  CHECK(names.size() == 3);

  double x = 3.0;
  symbol_table_t symbols;
  symbols.add_variable("x", x);
  expression_t square;
  CHECK(compile(symbols, "x^2", square));
  CHECK(nearly_equal(exprtk::derivative(square, x), 6.0, 1e-6));
  CHECK(x == 3.0);
  CHECK(nearly_equal(exprtk::integrate(square, x, 0.0, 1.0, 1000), 1.0 / 3.0,
                     1e-12));

  // readme.txt:4191-4197: an undefined symbol is a syntax error at its
  // position.
  {
    symbol_table_t empty;
    expression_t expression;
    expression.register_symbol_table(empty);
    parser_t parser;
    CHECK(!parser.compile("2 + (3 / log(1 + x))", expression));
    CHECK(parser.error_count() >= 1);
    if (parser.error_count() >= 1) {
      const exprtk::parser_error::type error = parser.get_error(0);
      CHECK(error.mode == exprtk::parser_error::e_syntax);
      CHECK(error.token.position == 17);
    }
  }
  {
    expression_t expression;
    expression.register_symbol_table(symbols);
    parser_t parser;
    CHECK(!parser.compile("x +", expression));
    CHECK(parser.error_count() >= 1);
  }

  // With the resolver enabled, unknown names become variables in the table.
  {
    expression_t expression;
    expression.register_symbol_table(symbols);
    parser_t parser;
    parser.enable_unknown_symbol_resolver();
    CHECK(parser.compile("z + 1", expression));
    CHECK(symbols.symbol_exists("z"));
    symbols.variable_ref("z") = 5.0;
    CHECK(expression.value() == 6.0);
  }
}

}  // namespace

int main() {
  scalars();
  vectors_and_strings();
  compositor();
  helpers_and_errors();
  if (failures != 0) {
    return 1;
  }
  std::printf("exprtk 0.0.3: C++ API OK\n");
  return 0;
}
