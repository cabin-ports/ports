#include <ableton/Link.hpp>

#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "ableton-link: line %d: %s\n", line, what);
    ++failures;
  }
}

}  // namespace

#define CHECK(cond) check((cond), #cond, __LINE__)

int main() {
  // Nothing here enables networking, so no peers ever appear.
  ableton::Link link(120.0);
  CHECK(!link.isEnabled());
  CHECK(link.numPeers() == 0);

  auto state = link.captureAppSessionState();
  CHECK(state.tempo() == 120.0);

  const auto now = link.clock().micros();
  state.setTempo(90.0, now);
  state.forceBeatAtTime(0.0, now, 4.0);
  link.commitAppSessionState(state);

  state = link.captureAppSessionState();
  CHECK(state.tempo() == 90.0);
  CHECK(std::abs(state.beatAtTime(now, 4.0)) < 1e-6);

  // One beat at 90 bpm is 2/3 s; beat 5 has phase 1 of a quantum of 4.
  const auto beat1 = state.timeAtBeat(1.0, 4.0);
  CHECK(std::abs((beat1 - now).count() - 666667) <= 1);
  CHECK(std::abs(state.phaseAtTime(state.timeAtBeat(5.0, 4.0), 4.0) - 1.0) < 1e-3);

  if (failures == 0) std::puts("ableton-link ok");
  return failures != 0;
}
