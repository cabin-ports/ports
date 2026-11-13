#include <indicators/block_progress_bar.hpp>
#include <indicators/display_width.hpp>
#include <indicators/dynamic_progress.hpp>
#include <indicators/indeterminate_progress_bar.hpp>
#include <indicators/multi_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/progress_spinner.hpp>

#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

// The bars pad each line to the terminal width, so the checks look for the
// drawn part only.
void test_progress_bar() {
  using namespace indicators;
  std::ostringstream os;
  ProgressBar bar{option::BarWidth{10},       option::Start{"["},     option::Fill{"="},
                  option::Lead{">"},          option::Remainder{" "}, option::End{"]"},
                  option::PrefixText{"copy "}, option::PostfixText{"files"},
                  option::ShowPercentage{true},   option::Stream{os}};
  bar.set_progress(50);
  CHECK(contains(os.str(), "\rcopy [=====>    ] 50% files"));
  CHECK(bar.current() == 50 && !bar.is_completed());
  bar.set_option(option::PostfixText{"done"});
  bar.set_progress(100);
  CHECK(contains(os.str(), "\rcopy [==========] 100% done"));
  CHECK(bar.is_completed() && os.str().back() == '\n');

  std::ostringstream down;
  ProgressBar countdown{option::BarWidth{4}, option::MaxProgress{4},
                        option::ProgressType{ProgressType::decremental}, option::Stream{down}};
  CHECK(countdown.current() == 4);
  countdown.tick();
  CHECK(countdown.current() == 3 && !countdown.is_completed());
  for (int i = 0; i < 3; ++i) countdown.tick();
  CHECK(countdown.current() == 0 && countdown.is_completed());
}

void test_block_progress_bar() {
  using namespace indicators;
  std::ostringstream os;
  BlockProgressBar bar{option::BarWidth{10}, option::Start{"["}, option::End{"]"}, option::Stream{os}};
  bar.set_progress(25.0f);
  // Two full blocks (U+2588) and a half block (U+258C).
  CHECK(contains(os.str(), "\r[\xE2\x96\x88\xE2\x96\x88\xE2\x96\x8C       ] 25%"));
  CHECK(bar.current() == 25 && !bar.is_completed());
  bar.set_progress(100.0f);
  std::string full = "[";
  for (int i = 0; i < 10; ++i) full += "\xE2\x96\x88";
  CHECK(contains(os.str(), full + "] 100%") && bar.current() == 100);
  // The block bar completes only past its maximum.
  CHECK(!bar.is_completed());
  bar.tick();
  CHECK(bar.is_completed() && bar.current() == 100 && os.str().back() == '\n');
}

void test_spinner() {
  using namespace indicators;
  std::ostringstream os;
  ProgressSpinner spinner{option::PrefixText{"load "}, option::PostfixText{"files"},
                          option::SpinnerStates{std::vector<std::string>{"-", "+"}}, option::Stream{os}};
  spinner.set_progress(25);
  CHECK(contains(os.str(), "load - 25% files"));
  spinner.tick();
  CHECK(contains(os.str(), "load + 26% files") && spinner.current() == 26);
  CHECK(!spinner.is_completed());
  spinner.mark_as_completed();
  CHECK(spinner.is_completed() && os.str().back() == '\n');

  std::ostringstream os2;
  ProgressSpinner automatic{option::Stream{os2}};
  automatic.set_progress(100);
  CHECK(!automatic.is_completed());
  automatic.tick();
  CHECK(automatic.is_completed() && automatic.current() == 100 && os2.str().back() == '\n');
}

void test_indeterminate() {
  using namespace indicators;
  std::ostringstream os;
  IndeterminateProgressBar bar{option::BarWidth{6}, option::Fill{"."}, option::Lead{"<>"},
                               option::Stream{os}};
  bar.tick();
  CHECK(contains(os.str(), "\r[.<>...]"));
  for (int i = 0; i < 3; ++i) bar.tick();
  CHECK(contains(os.str(), "\r[....<>]") && !bar.is_completed());
  bar.mark_as_completed();
  CHECK(bar.is_completed() && os.str().back() == '\n');
}

// Both containers also write line breaks and cursor moves to stdout.
void test_multi_and_dynamic() {
  using namespace indicators;
  std::ostringstream a, b;
  ProgressBar first{option::BarWidth{4}, option::ShowPercentage{true}, option::Stream{a}};
  ProgressBar second{option::BarWidth{4}, option::ShowPercentage{true}, option::Stream{b}};
  MultiProgress<ProgressBar, 2> both(first, second);
  both.set_progress<0>(std::size_t{50});
  both.set_progress<1>(std::size_t{100});
  CHECK(contains(a.str(), "[==> ] 50%") && contains(b.str(), "[====] 100%"));
  // Inside a container a completed bar ends no line of its own.
  CHECK(!both.is_completed<0>() && both.is_completed<1>() && b.str().back() != '\n');

  std::ostringstream c, d;
  ProgressBar third{option::BarWidth{4}, option::ShowPercentage{true}, option::Stream{c}};
  ProgressBar fourth{option::BarWidth{4}, option::ShowPercentage{true}, option::Stream{d}};
  DynamicProgress<ProgressBar> bars(third);
  const std::size_t added = bars.push_back(fourth);
  bars[added].set_progress(75);
  bars[0].set_progress(25);
  // A contained bar draws only when its container prints.
  bars.print_progress();
  CHECK(added == 1 && contains(d.str(), "[===>] 75%") && contains(c.str(), "[=>  ] 25%"));
}

void test_display_width() {
  CHECK(unicode::display_width(std::string("progress")) == 8);
  CHECK(unicode::display_width(std::wstring(L"abc")) == 3);
  // CJK ideographs take two columns.
  CHECK(unicode::display_width(std::wstring(L"\x4E2D\x6587")) == 4);
}

}  // namespace

int main() {
  test_progress_bar();
  test_block_progress_bar();
  test_spinner();
  test_indeterminate();
  test_multi_and_dynamic();
  test_display_width();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("indicators 2.3.0 ok");
  return 0;
}
