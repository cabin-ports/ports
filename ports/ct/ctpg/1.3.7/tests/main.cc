// Consumer test for cabin-ports/ctpg: generates LR(1) parsers at compile time,
// runs them on fixed input at run time and at compile time, and checks the
// results and the error report.
#include <ctpg/ctpg.hpp>

#include <charconv>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

using namespace ctpg;
using namespace ctpg::buffers;

namespace {

int failures = 0;

void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int to_int(std::string_view sv) {
  int i = 0;
  std::from_chars(sv.data(), sv.data() + sv.size(), i);
  return i;
}

// The readme's sum of a comma-separated list.
constexpr nterm<int> list("list");
constexpr char number_pattern[] = "[1-9][0-9]*";
constexpr regex_term<number_pattern> number("number");

constexpr parser sum_parser(list, terms(',', number), nterms(list),
                            rules(list(number) >= to_int,
                                  list(list, ',', number) >= [](int sum, char, const auto &n) {
                                    return sum + to_int(n);
                                  }));

// Arithmetic as in the readme: * over binary + and -, unary - over both, the
// binary operators left to right.  # is upstream's right-associative
// subtraction from tests/precedence.cpp, here at *'s precedence, so only the
// [3] override lifts unary - above it.
constexpr nterm<int> expr("expr");
constexpr char_term o_plus('+', 1, associativity::ltor);
constexpr char_term o_minus('-', 1, associativity::ltor);
constexpr char_term o_mul('*', 2, associativity::ltor);
constexpr char_term o_hash('#', 2, associativity::rtol);
constexpr char digits_pattern[] = "[0-9]+";
constexpr regex_term<digits_pattern> digits("digits");

constexpr parser calc(expr, terms(o_plus, o_minus, o_mul, o_hash, digits, '(', ')'), nterms(expr),
                      rules(expr(digits) >= [](const auto &d) { return to_int(d); },
                            expr(expr, '+', expr) >= [](int a, char, int b) { return a + b; },
                            expr(expr, '-', expr) >= [](int a, char, int b) { return a - b; },
                            expr(expr, '*', expr) >= [](int a, char, int b) { return a * b; },
                            expr(expr, '#', expr) >= [](int a, char, int b) { return a - b; },
                            expr('-', expr)[3] >= [](char, int a) { return -a; },
                            expr('(', expr, ')') >= [](char, int a, char) { return a; }));

// Upstream's compile-time case: count the stars while compiling.
constexpr nterm<int> stars("stars");
constexpr parser star_counter(stars, terms('*'), nterms(stars),
                              rules(stars('*') >= ftors::val(1),
                                    stars(stars, '*') >= [](int n, skip) { return n + 1; }));
static_assert(star_counter.parse(cstring_buffer("****")).value() == 4, "parsed at compile time");

std::optional<int> run(const std::string &text, std::ostream &errors) { return calc.parse(string_buffer(text.c_str()), errors); }

} // namespace

int main() {
  std::ostringstream errors;
  auto sum = sum_parser.parse(string_buffer("10, 20, 30"), errors);
  check(sum.has_value() && sum.value() == 60 && errors.str().empty(), "the readme's list sums to 60");

  auto bad = sum_parser.parse(string_buffer("1, 2, 3x"), errors);
  check(!bad.has_value(), "a stray character fails the parse");
  check(errors.str() == "[1:8] PARSE: Unexpected character: x\n", "the error names the line, column and character");

  std::ostringstream quiet;
  auto r = run("2+3*4", quiet);
  check(r.has_value() && r.value() == 14, "* binds tighter than +");
  r = run("(2+3)*4", quiet);
  check(r.has_value() && r.value() == 20, "parentheses group");
  r = run("10-3-2", quiet);
  check(r.has_value() && r.value() == 5, "binary - associates to the left");
  r = run("-2+3", quiet);
  check(r.has_value() && r.value() == 1, "unary - binds tighter than binary +");
  r = run("-3#2", quiet);
  check(r.has_value() && r.value() == -5, "the [3] override binds unary - tighter than #");
  check(quiet.str().empty(), "valid input reports nothing");

  constexpr auto at_compile_time = star_counter.parse(cstring_buffer("**"));
  static_assert(at_compile_time.has_value() && at_compile_time.value() == 2, "a constexpr result");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ctpg 1.3.7 C++ ok");
  return 0;
}
