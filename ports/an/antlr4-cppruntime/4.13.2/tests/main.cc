#include <antlr4-runtime.h>
#include <support/Utf8.h>
#include <tree/xpath/XPathLexer.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,        \
                   __LINE__, #cond);                                     \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

namespace {

antlr4::misc::Interval iv(ssize_t a, ssize_t b) { return {a, b}; }

class Recorder : public antlr4::tree::ParseTreeListener {
 public:
  std::string log;
  void visitTerminal(antlr4::tree::TerminalNode* node) override {
    log += "t:" + node->getText() + ";";
  }
  void visitErrorNode(antlr4::tree::ErrorNode*) override { log += "err;"; }
  void enterEveryRule(antlr4::ParserRuleContext*) override { log += "enter;"; }
  void exitEveryRule(antlr4::ParserRuleContext*) override { log += "exit;"; }
};

}  // namespace

int main() {
  CHECK(ANTLRCPP_VERSION_MAJOR == 4 && ANTLRCPP_VERSION_MINOR == 13 &&
        ANTLRCPP_VERSION_PATCH == 2);

  // The runtime's own generated XPath lexer over a char stream.
  {
    antlr4::ANTLRInputStream in("//expr/ID");
    CHECK(in.size() == 9);
    CHECK(in.LA(1) == '/');
    CHECK(in.getText(iv(2, 5)) == "expr");
    XPathLexer lexer(&in);
    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();
    CHECK(tokens.size() == 5);
    const size_t types[] = {XPathLexer::ANYWHERE,
                            XPathLexer::RULE_REF,
                            XPathLexer::ROOT,
                            XPathLexer::TOKEN_REF,
                            antlr4::Token::EOF};
    const char* texts[] = {"//", "expr", "/", "ID"};
    for (size_t i = 0; i < 5 && i < tokens.size(); ++i) {
      CHECK(tokens.get(i)->getType() == types[i]);
      if (i < 4) CHECK(tokens.get(i)->getText() == texts[i]);
    }
  }

  // A token stream over tokens built by hand.
  {
    std::vector<std::unique_ptr<antlr4::Token>> list;
    list.push_back(std::make_unique<antlr4::CommonToken>(7, "foo"));
    list.push_back(std::make_unique<antlr4::CommonToken>(8, "+"));
    list.push_back(std::make_unique<antlr4::CommonToken>(7, "bar"));
    antlr4::ListTokenSource source(std::move(list));
    antlr4::CommonTokenStream tokens(&source);
    tokens.fill();
    CHECK(tokens.size() == 4);
    CHECK(tokens.get(1)->getType() == 8);
    CHECK(tokens.get(2)->getText() == "bar");
    CHECK(tokens.get(3)->getType() == antlr4::Token::EOF);
    CHECK(tokens.LA(1) == 7);
    CHECK(tokens.getText(iv(0, 2)) == "foo+bar");
  }

  // Walk a hand-built parse tree.
  {
    antlr4::tree::ParseTreeTracker tracker;
    antlr4::CommonToken a(1, "a"), b(2, "b");
    auto* root = tracker.createInstance<antlr4::ParserRuleContext>();
    auto* inner = tracker.createInstance<antlr4::ParserRuleContext>(root, 0);
    root->addChild(
        tracker.createInstance<antlr4::tree::TerminalNodeImpl>(&a));
    root->addChild(inner);
    inner->addChild(
        tracker.createInstance<antlr4::tree::TerminalNodeImpl>(&b));
    CHECK(root->children.size() == 2);
    CHECK(inner->parent == root);
    Recorder rec;
    antlr4::tree::ParseTreeWalker::DEFAULT.walk(&rec, root);
    CHECK(rec.log == "enter;t:a;enter;t:b;exit;exit;");
    tracker.reset();
  }

  // Interval sets.
  {
    antlr4::misc::IntervalSet set = antlr4::misc::IntervalSet::of(1, 5);
    CHECK(set.contains(size_t{3}));
    CHECK(!set.contains(size_t{6}));
    CHECK(set.size() == 5);
    CHECK(set.toString() == "{1..5}");
    set.add(8);
    set.add(10, 12);
    CHECK(set.size() == 9);
    CHECK(set.toString() == "{1..5, 8, 10..12}");
    set.add(6);
    CHECK(set.toString() == "{1..6, 8, 10..12}");
  }

  // UTF-8 conversion of "hé€\U0001f600".
  {
    const std::string utf8 = "h\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    const std::u32string cps = U"hé€\U0001F600";
    CHECK(antlrcpp::Utf8::lenientDecode(utf8) == cps);
    CHECK(antlrcpp::Utf8::lenientEncode(cps) == utf8);
    auto bad = antlrcpp::Utf8::strictDecode("a\xFF");
    CHECK(!bad.has_value());
    CHECK(antlrcpp::Utf8::lenientDecode("a\xFF") == U"a�");
    auto one = antlrcpp::Utf8::decode(utf8.substr(1));
    CHECK(one.first == 0xE9 && one.second == 2);
  }

  return failures == 0 ? 0 : 1;
}
