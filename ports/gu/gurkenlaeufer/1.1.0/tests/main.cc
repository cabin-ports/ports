#define GURKE_STEP_NAME_PREFIX Probe

#include "gurkenlaeufer/Step.h"
#include "gurkenlaeufer/Parser.h"
#include "gurkenlaeufer/ParserStateFactory.h"
#include "gurkenlaeufer/ScenarioCollection.h"
#include "gurkenlaeufer/Run.h"

#include <cstdlib>
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

std::vector<std::string> trace;

struct Calculator {
    std::vector<int> values;
    std::string op;
};

}

STEP(".*I log in as (\\w+)")
{
    getFixture<Calculator>();
    trace.push_back("login:" + getNextParam<std::string>());
}

STEP(".*I have entered (\\d+) into the calculator$")
{
    auto calc = getFixture<Calculator>();
    const int n = getNextParam<int>();
    calc->values.push_back(n);
    trace.push_back("enter:" + std::to_string(n));
}

STEP(".*I press (\\w+)")
{
    gurkenlaeufer::ScenarioScope<Calculator> calc(this);
    calc->op = getNextParam<std::string>();
    trace.push_back("press:" + calc->op);
}

STEP(".*the result should be (\\d+) on the screen$")
{
    gurkenlaeufer::ScenarioScope<Calculator> calc(this);
    int sum = 0;
    for (int v : calc->values) {
        sum += v;
    }
    const int shown = getParam<int>(0u);
    trace.push_back("result:" + std::to_string(shown) + (shown == sum ? "=" : "!=") + std::to_string(sum));
}

STEP(".*a note")
{
    trace.push_back("note:" + getParam<std::string>(0u));
}

STEP(".*dup.*")
{
}

STEP(".*dup thing")
{
}

BEFORE("@Hooked")
{
    trace.push_back("before");
}

AFTER("@Hooked")
{
    trace.push_back("after");
}

static const char* const feature = R"(Feature: Adder
  # a full-line comment
  Background:
    Given I log in as bob

  @Hooked
  Scenario: Add
    Given I have entered 2 into the calculator
    And I have entered 40 into the calculator # trailing comment
    When I press add
    Then the result should be 42 on the screen

  Scenario Outline: Sum
    Given I have entered <a> into the calculator
    And I have entered <b> into the calculator
    Then the result should be <total> on the screen

  Examples:
    | a  | b  | total |
    | 20 | 30 | 50    |
    | 0  | 40 | 41    |

  @Ignore
  Scenario: Skipped
    Given I have entered 1 into the calculator

  Scenario: Docs
    Given a note
      """
      line one
      line two
      """
)";

static std::list<gurkenlaeufer::Scenario> parse(const std::string& text)
{
    auto scenarios = std::make_shared<gurkenlaeufer::ScenarioCollection>();
    gurkenlaeufer::Parser parser(
        gurkenlaeufer::IParserStateFactoryPtr(new gurkenlaeufer::ParserStateFactory(scenarios)));
    std::istringstream in(text);
    parser.parseStream(&in);
    parser.finish();
    return scenarios->getScenarios();
}

static std::vector<std::string> run(const gurkenlaeufer::Scenario& scenario)
{
    trace.clear();
    gurkenlaeufer::runScenario(scenario);
    return trace;
}

int main()
{
    const auto scenarios = parse(feature);

    // @Ignore drops one of the five parsed scenarios; the outline expands to two.
    check(scenarios.size() == 4u, "scenario count is 4");
    std::vector<std::string> names;
    for (const auto& s : scenarios) {
        names.push_back(s.description);
    }
    check(names == std::vector<std::string>({ "Add", "Sum example 0", "Sum example 1", "Docs" }),
        "scenario names");

    auto it = scenarios.begin();
    check(run(*it++) == std::vector<std::string>({ "before", "login:bob", "enter:2", "enter:40", "press:add", "result:42=42", "after" }),
        "Add: hooks, background, captures and order");
    check(run(*it++) == std::vector<std::string>({ "login:bob", "enter:20", "enter:30", "result:50=50" }),
        "outline example 0");
    check(run(*it++) == std::vector<std::string>({ "login:bob", "enter:0", "enter:40", "result:41!=40" }),
        "outline example 1 shows the mismatch");
    check(run(*it++) == std::vector<std::string>({ "login:bob", "note:line one\nline two" }),
        "doc string reaches the step");

    // A fixture is per scenario: a second run of Add starts from empty values.
    check(run(scenarios.front()).back() == "after" && trace[5] == "result:42=42", "fixtures reset between runs");

    auto unknown = parse("Scenario: Unknown\n  Given nothing matches this\n");
    check(unknown.size() == 1u, "unknown scenario parsed");
    bool threw = false;
    try {
        run(unknown.front());
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()) == "Did not find matching step for line: nothing matches this";
    }
    check(threw, "unmatched step throws");

    threw = false;
    try {
        run(parse("Scenario: Dup\n  Given dup thing\n").front());
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()).find("Ambiguous step definitions found for step: 'dup thing'") == 0;
    }
    check(threw, "ambiguous step throws");

    threw = false;
    try {
        gurkenlaeufer::Parser p(gurkenlaeufer::IParserStateFactoryPtr(
            new gurkenlaeufer::ParserStateFactory(std::make_shared<gurkenlaeufer::ScenarioCollection>())));
        p.parseFile("no/such.feature");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "missing feature file throws invalid_argument");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
