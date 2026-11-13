// cpp-peglib: grammars with semantic actions, packrat parsing, precedence
// climbing and left recursion, an AST, a parser built from combinators, UTF-8
// input, error reports, and one parser shared by two threads.
#include <peglib.h>

#include <any>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* expr, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, expr);
    ++failures;
  }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

}  // namespace

int main() {
  using peg::SemanticValues;
  using std::any_cast;

  // README's calculator: actions turn the matches into values.
  peg::parser calc(R"(
    Additive       <- Multiplicative '+' Additive / Multiplicative
    Multiplicative <- Primary '*' Multiplicative / Primary
    Primary        <- '(' Additive ')' / Number
    Number         <- < [0-9]+ >
    %whitespace    <- [ \t]*
  )");
  CHECK(static_cast<bool>(calc));
  calc["Additive"] = [](const SemanticValues& vs) {
    return vs.choice() == 0 ? any_cast<int>(vs[0]) + any_cast<int>(vs[1])
                            : any_cast<int>(vs[0]);
  };
  calc["Multiplicative"] = [](const SemanticValues& vs) {
    return vs.choice() == 0 ? any_cast<int>(vs[0]) * any_cast<int>(vs[1])
                            : any_cast<int>(vs[0]);
  };
  int numbers = 0;
  calc["Number"] = [&](const SemanticValues& vs) {
    ++numbers;
    return vs.token_to_number<int>();
  };
  // Packrat parsing reuses each Number match instead of parsing it again
  // for every alternative that backtracks over it.
  calc.enable_packrat_parsing();
  int value = 0;
  CHECK(calc.parse(" (1 + 2) * 3 ", value) && value == 9 && numbers == 3);

  // A precedence instruction and a left-recursive rule both make '-' and
  // '/' left associative.
  peg::parser infix(R"(
    EXPRESSION             <- INFIX_EXPRESSION(ATOM, OPERATOR)
    ATOM                   <- NUMBER / '(' EXPRESSION ')'
    OPERATOR               <- < [-+/*] >
    NUMBER                 <- < '-'? [0-9]+ >
    %whitespace            <- [ \t]*
    INFIX_EXPRESSION(A, O) <- A (O A)* {
      precedence
        L + -
        L * /
    }
  )");
  infix["INFIX_EXPRESSION"] = [](const SemanticValues& vs) {
    long result = any_cast<long>(vs[0]);
    if (vs.size() > 1) {
      const long rhs = any_cast<long>(vs[2]);
      switch (any_cast<char>(vs[1])) {
        case '+': result += rhs; break;
        case '-': result -= rhs; break;
        case '*': result *= rhs; break;
        default: result /= rhs; break;
      }
    }
    return result;
  };
  CHECK(static_cast<bool>(infix));
  infix["OPERATOR"] = [](const SemanticValues& vs) { return *vs.sv().data(); };
  infix["NUMBER"] = [](const SemanticValues& vs) {
    return vs.token_to_number<long>();
  };
  long number = 0;
  CHECK(infix.parse(" -1 + (1 + 2) * 3 - -1", number) && number == 9);
  CHECK(infix.parse("10 - 4 - 3 * 2 / 3", number) && number == 4);

  peg::parser left(R"(
    Expr   <- Expr '-' Term / Term
    Term   <- Term '/' Number / Number
    Number <- < [0-9]+ >
    %whitespace <- [ \t]*
  )");
  CHECK(static_cast<bool>(left));
  left["Expr"] = [](const SemanticValues& vs) {
    return vs.choice() == 0 ? any_cast<long>(vs[0]) - any_cast<long>(vs[1])
                            : any_cast<long>(vs[0]);
  };
  left["Term"] = [](const SemanticValues& vs) {
    return vs.choice() == 0 ? any_cast<long>(vs[0]) / any_cast<long>(vs[1])
                            : any_cast<long>(vs[0]);
  };
  left["Number"] = [](const SemanticValues& vs) {
    return vs.token_to_number<long>();
  };
  // Two threads parse with one parser at once, its first parses included.
  long sums[2] = {0, 0};
  std::thread worker([&] { left.parse("100 - 1 - 1", sums[0]); });
  left.parse("9 / 3 - 1", sums[1]);
  worker.join();
  CHECK(sums[0] == 98 && sums[1] == 2);
  CHECK(left.parse("1 - 2 - 3", number) && number == -4);
  CHECK(left.parse("8 / 4 / 2", number) && number == 1);

  // An AST, before and after optimize_ast; its tokens point into source.
  peg::parser list(R"(
    LIST   <- ITEM (',' ITEM)*
    ITEM   <- NAME / NUMBER
    NAME   <- < [a-z]+ >
    NUMBER <- < [0-9]+ >
    %whitespace <- [ \t]*
  )");
  CHECK(static_cast<bool>(list));
  list.enable_ast();
  const std::string source = "abc, 42";
  std::shared_ptr<peg::Ast> ast;
  CHECK(list.parse(source, ast));
  CHECK(peg::ast_to_s(ast) ==
        "+ LIST\n  + ITEM/0\n    - NAME (abc)\n  + ITEM/1\n    - NUMBER (42)\n");
  ast = list.optimize_ast(ast);
  CHECK(peg::ast_to_s(ast) ==
        "+ LIST\n  - ITEM/0[NAME] (abc)\n  - ITEM/1[NUMBER] (42)\n");
  CHECK(ast->nodes.size() == 2 && ast->nodes[1]->name == "NUMBER" &&
        ast->nodes[1]->original_name == "ITEM" &&
        ast->nodes[1]->token_to_number<int>() == 42 &&
        ast->nodes[1]->column == 6 && ast->nodes[1]->position == 5);

  // README's parser built from combinators instead of grammar text.
  std::vector<std::string> tags;
  peg::Definition ROOT, TAG_NAME, _;
  ROOT <= peg::seq(_, peg::zom(peg::seq(peg::chr('['), TAG_NAME,
                                        peg::chr(']'), _)));
  TAG_NAME <= peg::oom(peg::seq(peg::npd(peg::chr(']')), peg::dot())),
      [&](const SemanticValues& vs) { tags.push_back(vs.token_to_string()); };
  _ <= peg::zom(peg::cls(" \t"));
  CHECK(ROOT.parse(" [tag1] [tag:2] [tag-3] ").ret);
  CHECK(tags == std::vector<std::string>({"tag1", "tag:2", "tag-3"}));

  // '.' matches one code point; error columns count code points.
  peg::parser utf8("S <- < . > '!' [0-9]+");
  CHECK(static_cast<bool>(utf8));
  std::string token;
  utf8["S"] = [&](const SemanticValues& vs) { token = vs.token_to_string(); };
  CHECK(utf8.parse("\xC3\xA9!1") && token == "\xC3\xA9");
  std::string logged;
  utf8.set_logger([&](std::size_t line, std::size_t col,
                      const std::string& msg, const std::string&) {
    logged += std::to_string(line) + ":" + std::to_string(col) + ": " + msg;
  });
  CHECK(!utf8.parse("\xC3\xA9!x"));
  CHECK(logged == "1:3: syntax error, unexpected 'x', expecting <S>.");

  // Errors reach the logger as text and the error reporter as data.
  logged.clear();
  std::vector<peg::ErrorReport> reports;
  calc.set_logger([&](std::size_t line, std::size_t col,
                      const std::string& msg, const std::string&) {
    logged += std::to_string(line) + ":" + std::to_string(col) + ": " + msg;
  });
  calc.set_error_reporter(
      [&](const peg::ErrorReport& report) { reports.push_back(report); });
  CHECK(!calc.parse("1 + (2 * x)", value));
  CHECK(logged == "1:10: syntax error, unexpected 'x', expecting '(', <Number>.");
  CHECK(reports.size() == 1 && reports[0].line == 1 &&
        reports[0].col == 10 && reports[0].position == 9 &&
        reports[0].message.empty() &&
        reports[0].unexpected_token == "x" && reports[0].label == "Number" &&
        reports[0].expected_literals == std::vector<std::string>({"("}) &&
        reports[0].expected_rules == std::vector<std::string>({"Number"}));

  peg::parser codes(R"(
    START       <- CODE (',' CODE)*
    CODE        <- < '0x' [a-fA-F0-9]+ > { error_message 'code format error...' }
    %whitespace <- [ \t]*
  )");
  CHECK(static_cast<bool>(codes));
  logged.clear();
  codes.set_logger([&](std::size_t line, std::size_t col,
                       const std::string& msg, const std::string&) {
    logged += std::to_string(line) + ":" + std::to_string(col) + ": " + msg;
  });
  CHECK(!codes.parse("0x1234,0x@@@@,0xABCD"));
  CHECK(logged == "1:8: code format error...");

  // A grammar that names an undefined rule does not load.
  peg::parser broken;
  logged.clear();
  broken.set_logger([&](std::size_t line, std::size_t col,
                        const std::string& msg, const std::string&) {
    logged += std::to_string(line) + ":" + std::to_string(col) + ": " + msg;
  });
  CHECK(!broken.load_grammar("A <- B 'x'\n"));
  CHECK(logged == "1:6: 'B' is not defined.");

  if (failures != 0) {
    return 1;
  }
  std::puts("cpp-peglib ok");
  return 0;
}
