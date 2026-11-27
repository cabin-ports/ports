#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "mp/format.h"
#include "mp/nl.h"
#include "mp/problem.h"
#include "mp/solver-base.h"

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// A text NL model: minimize 3x + 4y subject to 2x + y >= 1, 0 <= x <= 5,
// y >= 0.
static const char kNL[] =
    "g3 1 1 0\n"
    " 2 1 1 0 0\n"
    " 0 0\n"
    " 0 0\n"
    " 0 0 0\n"
    " 0 0 0 1\n"
    " 0 0 0 0 0\n"
    " 2 2\n"
    " 0 0\n"
    " 0 0 0 0 0\n"
    "C0\n"
    "n0\n"
    "O0 0\n"
    "n0\n"
    "r\n"
    "2 1\n"
    "b\n"
    "0 0 5\n"
    "2 0\n"
    "k1\n"
    "1\n"
    "J0 2\n"
    "0 2\n"
    "1 1\n"
    "G0 2\n"
    "0 3\n"
    "1 4\n";

struct CaptureOutput : mp::OutputHandler {
  std::string text;
  void HandleOutput(fmt::CStringRef output) override { text += output.c_str(); }
};

struct VersionSolver : mp::BasicSolver {
  VersionSolver() : mp::BasicSolver("vsolver", "Version Solver", 0, 0) {}
};

#if defined(__linux__) && defined(__x86_64__)
#define EXPECTED_SYSINFO "Linux x86_64"
#elif defined(__linux__) && defined(__aarch64__)
#define EXPECTED_SYSINFO "Linux aarch64"
#elif defined(__APPLE__) && defined(__x86_64__)
#define EXPECTED_SYSINFO "Darwin x86_64"
#elif defined(__APPLE__) && defined(__aarch64__)
#define EXPECTED_SYSINFO "Darwin arm64"
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#define EXPECTED_SYSINFO "Windows AMD64"
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
#define EXPECTED_SYSINFO "Windows ARM64"
#endif

int main() {
  // MP_DATE is a PUBLIC definition of upstream's mp target.
  check(MP_DATE == 20261029, "MP_DATE reaches the consumer");

  // mp's bundled fmt (format.cc).
  check(fmt::format("{}+{}={}", 1, 2, 3) == "1+2=3", "fmt::format integers");
  check(fmt::format("[{:>7.3f}]", 3.14159) == "[  3.142]",
        "fmt::format precision and alignment");
  fmt::MemoryWriter w;
  w << "x" << 42 << ':' << fmt::hex(255);
  check(w.str() == "x42:ff", "fmt::MemoryWriter");

  // Expression tables of the generated src/expr-info.cc.
  check(std::strcmp(mp::expr::str(mp::expr::MUL), "*") == 0, "expr::str(MUL)");
  check(mp::expr::nl_opcode(mp::expr::MUL) == 2, "nl_opcode(MUL)");
  check(mp::expr::nl_opcode(mp::expr::FLOOR) == 13, "nl_opcode(FLOOR)");

  // A problem built through the API.
  mp::Problem p;
  mp::Problem::Variable x = p.AddVar(0, 10);
  p.AddVar(-1, 1, mp::var::INTEGER);
  mp::Problem::MutAlgebraicCon con = p.AddCon(1, 5);
  con.set_linear_expr(2).AddTerm(0, 3);
  p.algebraic_con(0).linear_expr().AddTerm(1, -4);
  check(p.num_vars() == 2, "num_vars");
  check(p.num_algebraic_cons() == 1, "num_algebraic_cons");
  check(x.lb() == 0 && x.ub() == 10, "var 0 bounds");
  check(p.var(1).type() == mp::var::INTEGER, "var 1 type");
  check(p.var(1).lb() == -1 && p.var(1).ub() == 1, "var 1 bounds");
  const mp::LinearExpr &le = p.algebraic_con(0).linear_expr();
  check(le.num_terms() == 2 && le.var_index(0) == 0 && le.coef(0) == 3 &&
            le.var_index(1) == 1 && le.coef(1) == -4,
        "constraint terms");
  check(p.algebraic_con(0).lb() == 1 && p.algebraic_con(0).ub() == 5,
        "constraint bounds");

  // A problem read from an NL string, through problem.cc's instantiation.
  mp::Problem nl;
  mp::ReadNLString(kNL, nl);
  check(nl.num_vars() == 2, "NL num_vars");
  check(nl.num_algebraic_cons() == 1, "NL num_algebraic_cons");
  check(nl.num_objs() == 1, "NL num_objs");
  check(nl.obj(0).type() == mp::obj::MIN, "NL objective sense");
  const mp::LinearExpr &obj = nl.obj(0).linear_expr();
  check(obj.num_terms() == 2 && obj.var_index(0) == 0 && obj.coef(0) == 3 &&
            obj.var_index(1) == 1 && obj.coef(1) == 4,
        "NL objective terms");
  const mp::LinearExpr &row = nl.algebraic_con(0).linear_expr();
  check(row.num_terms() == 2 && row.var_index(0) == 0 && row.coef(0) == 2 &&
            row.var_index(1) == 1 && row.coef(1) == 1,
        "NL constraint terms");
  check(nl.algebraic_con(0).lb() == 1 &&
            std::isinf(nl.algebraic_con(0).ub()) &&
            nl.algebraic_con(0).ub() > 0,
        "NL constraint bounds");
  check(nl.var(0).lb() == 0 && nl.var(0).ub() == 5, "NL var 0 bounds");
  check(nl.var(1).lb() == 0 && std::isinf(nl.var(1).ub()), "NL var 1 bounds");

  // solver.cc's version line carries MP_SYSINFO and MP_DATE.
  VersionSolver s;
  CaptureOutput out;
  s.set_output_handler(&out);
  s.ShowVersion();
  check(out.text == "Version Solver (" EXPECTED_SYSINFO "), MP(20261029)\n",
        "ShowVersion line");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed; version line: %s", failures,
                 out.text.c_str());
    return 1;
  }
  std::puts("ampl-mp ok");
  return 0;
}
