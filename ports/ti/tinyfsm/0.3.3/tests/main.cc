// Consumer test for cabin-ports/tinyfsm: a coin-operated turnstile (entry and
// exit actions, a guarded transition with an action, per-state data) and a
// Mealy lamp, driven together through FsmList and then reset through
// StateList, with exact checks of the current state and the action trace.
// Written in C++11, upstream's minimum.
#include <tinyfsm.hpp>

#include <cstdio>
#include <string>

struct Coin : tinyfsm::Event {
  explicit Coin(int c) : cents(c) {}
  int cents;
};
struct Push : tinyfsm::Event {};
struct Kick : tinyfsm::Event {};  // no state reacts to it

static std::string trace;

struct Turnstile : tinyfsm::Fsm<Turnstile> {
  void react(tinyfsm::Event const &) {}  // unhandled events
  virtual void react(Coin const &) {}
  virtual void react(Push const &) {}
  virtual void entry() {}
  virtual void exit() {}
  static void reset();
};

struct Locked : Turnstile {
  Locked() : rejected(0) {}
  void entry() override { trace += "L+"; }
  void exit() override { trace += "L-"; }
  void react(Coin const &coin) override;
  void react(Push const &) override { trace += "!"; }
  int rejected;
};

struct Unlocked : Turnstile {
  Unlocked() : passages(0) {}
  void entry() override { trace += "U+"; }
  void exit() override { trace += "U-"; }
  void react(Coin const &) override { trace += "r"; }  // refunds extra coins
  void react(Push const &) override;
  int passages;
};

void Locked::react(Coin const &coin) {
  // Unlocks only for 50 cents or more; the action runs between exit and entry.
  // transit must be the last statement of a reaction (doc/40-Usage.md).
  transit<Unlocked>([] { trace += "a"; },
                    [this, &coin]() -> bool {
                      if (coin.cents >= 50) return true;
                      ++rejected;
                      return false;
                    });
}

void Unlocked::react(Push const &) {
  ++passages;
  transit<Locked>();
}

void Turnstile::reset() { tinyfsm::StateList<Locked, Unlocked>::reset(); }

FSM_INITIAL_STATE(Turnstile, Locked)

static int switches = 0;

struct Lamp : tinyfsm::MealyMachine<Lamp> {
  void react(tinyfsm::Event const &) {}
  virtual void react(Push const &) {}
  static void count() { ++switches; }
};

struct Dark : Lamp {
  void react(Push const &) override;
};

struct Lit : Lamp {
  void react(Push const &) override;
};

void Dark::react(Push const &) { transit<Lit>(count); }
void Lit::react(Push const &) { transit<Dark>(count); }

FSM_INITIAL_STATE(Lamp, Dark)

using machines = tinyfsm::FsmList<Turnstile, Lamp>;

static_assert(tinyfsm::is_same_fsm<Turnstile, Unlocked>::value && !tinyfsm::is_same_fsm<Turnstile, Lit>::value,
              "states belong to their own machine");

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  machines::start();
  check(Turnstile::is_in_state<Locked>() && Lamp::is_in_state<Dark>() && trace == "L+", "start enters the initial states");

  Turnstile::dispatch(Coin(20));
  check(Turnstile::is_in_state<Locked>() && Turnstile::state<Locked>().rejected == 1 && trace == "L+",
        "the guard rejects 20 cents without leaving the state");

  Turnstile::dispatch(Coin(50));
  check(Turnstile::is_in_state<Unlocked>() && trace == "L+L-aU+", "exit, action, entry in order");

  Turnstile::dispatch(Coin(100));
  check(Turnstile::is_in_state<Unlocked>() && trace == "L+L-aU+r", "a reaction without a transition");

  machines::dispatch(Push());
  check(Turnstile::is_in_state<Locked>() && Turnstile::state<Unlocked>().passages == 1 && Lamp::is_in_state<Lit>() &&
            switches == 1,
        "FsmList dispatches to both machines");

  machines::dispatch(Push());
  check(Turnstile::is_in_state<Locked>() && Lamp::is_in_state<Dark>() && switches == 2 && trace == "L+L-aU+rU-L+!",
        "the second push");

  machines::dispatch(Kick());
  check(Turnstile::is_in_state<Locked>() && Lamp::is_in_state<Dark>() && switches == 2 && trace == "L+L-aU+rU-L+!",
        "an unhandled event changes nothing");

  Turnstile::dispatch(Coin(50));
  check(Turnstile::is_in_state<Unlocked>() && trace == "L+L-aU+rU-L+!L-aU+", "unlocked again");

  Lamp::dispatch(Push());
  check(Lamp::is_in_state<Lit>() && switches == 3 && Turnstile::is_in_state<Unlocked>(),
        "dispatch to one machine leaves the other alone");

  machines::reset();
  check(Turnstile::state<Locked>().rejected == 0 && Turnstile::state<Unlocked>().passages == 0,
        "StateList::reset rebuilds the state instances");
  machines::start();
  check(Turnstile::is_in_state<Locked>() && Lamp::is_in_state<Dark>() && trace == "L+L-aU+rU-L+!L-aU+L+",
        "restart enters the initial states again");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("tinyfsm 0.3.3 C++ ok");
  return 0;
}
