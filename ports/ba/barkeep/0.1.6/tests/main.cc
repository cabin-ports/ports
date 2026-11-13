#include <barkeep/barkeep.h>

#include <atomic>
#include <cstdio>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

// In no-tty mode every frame is one line, and done() renders a final frame
// followed by one more newline, so the output ends in that frame plus "\n\n".
// The number of earlier frames depends on timing and is not checked.
std::string FinalFrame(const std::string& out) {
  if (out.size() < 2 || out.compare(out.size() - 2, 2, "\n\n") != 0) {
    return "<no final frame>";
  }
  const std::string frames = out.substr(0, out.size() - 2);
  const std::size_t start = frames.rfind('\n');
  return start == std::string::npos ? frames : frames.substr(start + 1);
}

}  // namespace

int main() {
  {
    std::ostringstream out;
    std::atomic<int> items{0};
    barkeep::CounterConfig cfg;
    cfg.out = &out;
    cfg.message = "items";
    cfg.interval = 0.001;
    cfg.no_tty = true;
    auto counter = barkeep::Counter(&items, cfg);
    check(counter->running(), "counter shows on construction");
    for (int i = 0; i < 250; ++i) ++items;
    counter->done();
    check(!counter->running(), "counter stops on done()");
    const std::string frame = FinalFrame(out.str());
    check(frame == "items 250 ", "counter final frame: [" + frame + "]");
  }
  {
    std::ostringstream out;
    std::atomic<int> files{0};
    barkeep::ProgressBarConfig<int> cfg;
    cfg.out = &out;
    cfg.total = 40;
    cfg.message = "files";
    cfg.style = barkeep::Bars;
    cfg.interval = 0.001;
    cfg.no_tty = true;
    auto bar = barkeep::ProgressBar(&files, cfg);
    for (int i = 0; i < 30; ++i) ++files;
    bar->done();
    // 30/40 of the 30-column bar is 22 columns filled and 8 empty.
    const std::string expected = "files  75.00% |" + std::string(22, '|') +
                                 std::string(8, ' ') + "| 30/40 ";
    const std::string frame = FinalFrame(out.str());
    check(frame == expected, "progress bar final frame: [" + frame + "]");
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("barkeep consumer test passed");
  return 0;
}
