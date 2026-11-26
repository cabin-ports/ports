#include "hsm/hsm.h"

#include <cstdio>

namespace {

struct Locked {};
struct Unlocked {};
struct Resetting {};
struct Idle {};
struct Ringing {};

struct Push {};
struct Coin {};
struct Reset {};
struct Kick {};
struct Calm {};

int beeps = 0;
int blinks = 0;
int resets = 0;
int alarms = 0;
bool coinAccepted = true;

const auto beep = [](auto, auto, auto) { ++beeps; };
const auto blink = [](auto, auto, auto) { ++blinks; };
const auto reset = [](auto, auto, auto) { ++resets; };
const auto alarm = [](auto, auto, auto) { ++alarms; };
const auto accepting = [](auto, auto, auto) { return coinAccepted; };

struct Alarm {
  static constexpr auto make_transition_table() {
    return hsm::transition_table(
        *hsm::state<Idle> + hsm::event<Kick> / alarm = hsm::state<Ringing>,
        hsm::state<Ringing> + hsm::event<Calm> = hsm::state<Idle>);
  }
};

struct Turnstile {
  static constexpr auto make_transition_table() {
    return hsm::transition_table(
        *hsm::state<Locked> + hsm::event<Push> / beep = hsm::state<Locked>,
        hsm::state<Locked> + hsm::event<Coin>[accepting] / blink = hsm::state<Unlocked>,
        hsm::state<Unlocked> + hsm::event<Push> = hsm::state<Locked>,
        hsm::state<Unlocked> + hsm::event<Coin> / blink = hsm::state<Unlocked>,
        hsm::state<Unlocked> + hsm::event<Reset> = hsm::state<Resetting>,
        hsm::state<Resetting> / reset = hsm::state<Locked>,
        hsm::state<Locked> + hsm::event<Kick> = hsm::state<Alarm>,
        hsm::state<Alarm> + hsm::event<Push> = hsm::state<Locked>);
  }
};

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  hsm::sm<Turnstile> sm;
  check(sm.is(hsm::state<Locked>), "starts Locked");

  // Action without guard.
  check(sm.process_event(Push{}), "Push in Locked is processed");
  check(beeps == 1 && sm.is(hsm::state<Locked>), "Push beeps and stays Locked");

  // Failing guard: no transition, no action, event reported unprocessed.
  coinAccepted = false;
  check(!sm.process_event(Coin{}), "Coin refused by guard");
  check(blinks == 0 && sm.is(hsm::state<Locked>), "refused Coin leaves Locked");

  // Passing guard.
  coinAccepted = true;
  check(sm.process_event(Coin{}), "Coin accepted");
  check(blinks == 1 && sm.is(hsm::state<Unlocked>), "Coin unlocks");
  check(sm.process_event(Coin{}) && blinks == 2 && sm.is(hsm::state<Unlocked>),
        "second Coin blinks and stays Unlocked");
  check(sm.process_event(Push{}) && beeps == 1 && sm.is(hsm::state<Locked>),
        "Push relocks without beeping");

  // Anonymous transition: Resetting completes into Locked at once.
  check(sm.process_event(Coin{}), "unlock before Reset");
  check(sm.process_event(Reset{}), "Reset processed");
  check(resets == 1 && sm.is(hsm::state<Locked>), "anonymous Resetting -> Locked ran its action");

  // Sub-state machine: entering starts in its initial state.
  check(sm.process_event(Kick{}), "Kick enters Alarm");
  check(sm.is(hsm::state<Alarm>, hsm::state<Idle>) && sm.parent_is(hsm::state<Alarm>),
        "Alarm starts in Idle");
  check(sm.process_event(Kick{}) && alarms == 1, "inner Kick runs the alarm action");
  check(sm.is(hsm::state<Alarm>, hsm::state<Ringing>), "inner state is Ringing");
  check(sm.process_event(Calm{}) && sm.is(hsm::state<Alarm>, hsm::state<Idle>),
        "Calm returns to Idle");
  check(sm.process_event(Push{}) && sm.is(hsm::state<Turnstile>, hsm::state<Locked>),
        "Push in the sub-machine leaves it for Locked");
  check(beeps == 1 && blinks == 3 && resets == 1 && alarms == 1, "final side-effect counts");

  return failures == 0 ? 0 : 1;
}
