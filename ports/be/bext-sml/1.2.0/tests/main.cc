// Consumer test for cabin-ports/bext-sml: drives [Boost::ext].SML state
// machines through the public API and checks every observable result.
// States are state<T> types rather than "name"_s literals, which MSVC lacks
// before C++20.
#include <boost/sml.hpp>
#include <boost/sml/utility/dispatch_table.hpp>

#include <cstdio>
#include <deque>
#include <queue>
#include <stdexcept>
#include <string>

namespace sml = boost::sml;

namespace {

int failures = 0;

void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Injected into guards and actions by reference.
struct journal {
    std::string trail;
    int code = 0;
};

// A door: guards, actions, event data, a dependency, entry and exit actions.
struct open_door {};
struct close_door {};
struct lock_door {
    int code;
};
struct unlock_door {
    int code;
};

struct closed {
    static constexpr const char *c_str() { return "closed"; }
};
struct opened {
    static constexpr const char *c_str() { return "opened"; }
};
struct locked {
    static constexpr const char *c_str() { return "locked"; }
};

struct door {
    auto operator()() const {
        using namespace sml;
        const auto store_code = [](const lock_door &e, journal &j) {
            j.code = e.code;
            j.trail += 'L';
        };
        // Reads the same journal through a const reference.
        const auto code_matches = [](const unlock_door &e, const journal &j) { return e.code == j.code; };
        // clang-format off
        return make_transition_table(
            *state<closed> + event<open_door> / [](journal &j) { j.trail += 'o'; } = state<opened>,
             state<opened> + event<close_door> / [](journal &j) { j.trail += 'c'; } = state<closed>,
             state<closed> + event<lock_door> / store_code = state<locked>,
             state<locked> + event<unlock_door>[code_matches] / [](journal &j) { j.trail += 'U'; } = state<closed>,
             state<locked> + on_entry<_> / [](journal &j) { j.trail += '+'; },
             state<locked> + on_exit<_> / [](journal &j) { j.trail += '-'; });
        // clang-format on
    }
};

void test_door() {
    using namespace sml;
    journal j;
    sm<door> door_sm{j};
    check(door_sm.is(state<closed>), "the door starts closed");
    check(door_sm.process_event(open_door{}), "open_door is handled");
    check(door_sm.is(state<opened>), "open_door opens the door");
    check(!door_sm.process_event(lock_door{7}), "an open door cannot be locked");
    check(door_sm.is(state<opened>), "an unhandled event leaves the state alone");
    check(door_sm.process_event(close_door{}), "close_door is handled");
    check(door_sm.process_event(lock_door{7}), "lock_door is handled");
    check(door_sm.is(state<locked>), "lock_door locks the door");
    check(j.code == 7, "an action reads the event and writes the dependency");
    check(!door_sm.process_event(unlock_door{3}), "a failing guard rejects the event");
    check(door_sm.is(state<locked>), "a rejected event leaves the door locked");
    check(door_sm.process_event(unlock_door{7}), "a passing guard accepts the event");
    check(door_sm.is(state<closed>), "unlock_door with the right code unlocks");
    // Entry runs after the transition action; exit runs before it.
    check(j.trail == "ocL+-U", "actions, entry and exit run in order");
}

// The testing policy sets the current state directly.
void test_testing_policy() {
    using namespace sml;
    journal j;
    j.code = 5;
    sm<door, sml::testing> door_sm{j};
    door_sm.set_current_states(state<locked>);
    check(door_sm.is(state<locked>), "set_current_states moves the machine");
    check(door_sm.process_event(unlock_door{5}), "the forced state handles its events");
    check(door_sm.is(state<closed>) && j.trail == "-U", "a forced state runs its exit action");
}

// The logger policy sees events, guard results, actions and state changes.
struct trail_logger {
    std::string trail;
    template <class SM, class TEvent>
    void log_process_event(const TEvent &) {}
    template <class SM>
    void log_process_event(const unlock_door &) {
        trail += 'E';
    }
    template <class SM, class TGuard, class TEvent>
    void log_guard(const TGuard &, const TEvent &, bool result) {
        trail += result ? 'T' : 'F';
    }
    template <class SM, class TAction, class TEvent>
    void log_action(const TAction &, const TEvent &) {
        trail += 'A';
    }
    template <class SM, class TSrcState, class TDstState>
    void log_state_change(const TSrcState &src, const TDstState &dst) {
        trail += '[';
        trail += src.c_str();
        trail += '>';
        trail += dst.c_str();
        trail += ']';
    }
};

void test_logger() {
    using namespace sml;
    journal j;
    trail_logger log;
    sm<door, sml::logger<trail_logger>> door_sm{j, log};
    door_sm.process_event(lock_door{9});
    check(log.trail == "[closed>locked]AA", "the logger sees a transition, its action and the entry action");
    log.trail.clear();
    door_sm.process_event(unlock_door{1});
    door_sm.process_event(unlock_door{9});
    check(log.trail == "EFETA[locked>closed]A", "the logger sees guards, exit, transition and action");
    check(door_sm.is(state<closed>), "a logged machine still transitions");
}

// Anonymous transitions, unexpected events, exceptions, orthogonal regions,
// the terminate state X and visiting the current states.
struct start {};
struct fail {};
struct crash {};
struct stray {};

struct idle {
    static constexpr const char *c_str() { return "idle"; }
};
struct busy {
    static constexpr const char *c_str() { return "busy"; }
};
struct done {
    static constexpr const char *c_str() { return "done"; }
};
struct guarding {
    static constexpr const char *c_str() { return "guarding"; }
};

struct worker {
    auto operator()() const {
        using namespace sml;
        // clang-format off
        return make_transition_table(
            *state<idle> + event<start> = state<busy>,
             state<busy> / [](journal &j) { j.trail += 'a'; } = state<done>,
             state<done> + event<fail> / [] { throw std::runtime_error("fail"); },
             state<done> + event<crash> / [] { throw 42; },
             state<done> + unexpected_event<stray> / [](journal &j) { j.trail += 'u'; },
            *state<guarding> + exception<std::runtime_error> / [](journal &j) { j.trail += 'r'; },
             state<guarding> + exception<_> / [](journal &j) { j.trail += 'x'; } = X);
        // clang-format on
    }
};

std::string current_states(const sml::sm<worker> &worker_sm) {
    std::string names;
    worker_sm.visit_current_states([&](auto state) {
        names += state.c_str();
        names += ';';
    });
    return names;
}

void test_worker() {
    using namespace sml;
    journal j;
    sm<worker> worker_sm{j};
    check(worker_sm.is(state<idle>, state<guarding>), "each region starts in its initial state");
    check(current_states(worker_sm) == "idle;guarding;", "visit_current_states lists every region");
    worker_sm.process_event(start{});
    check(worker_sm.is(state<done>, state<guarding>), "an anonymous transition runs right after entry");
    worker_sm.process_event(stray{});
    check(j.trail == "au", "an event no transition handles reaches unexpected_event");
    worker_sm.process_event(fail{});
    check(worker_sm.is(state<done>, state<guarding>), "a matching exception handler keeps its region");
    check(j.trail == "aur", "a std::runtime_error reaches exception<std::runtime_error>");
    worker_sm.process_event(crash{});
    check(worker_sm.is(state<done>, X), "the catch-all exception handler terminates its region");
    check(j.trail == "aurx", "any other exception reaches exception<_>");
    check(current_states(worker_sm) == "done;terminate;", "visit_current_states names X");
}

// A composite state with history: re-entering resumes where it left off.
struct step {};
struct enter {};
struct leave {};

struct first {};
struct second {};
struct lobby {};

struct inner {
    auto operator()() const {
        using namespace sml;
        // clang-format off
        return make_transition_table(
            state<first>(H) + event<step> = state<second>,
            state<second> + event<step> = X);
        // clang-format on
    }
};

struct outer {
    auto operator()() const {
        using namespace sml;
        // clang-format off
        return make_transition_table(
            *state<lobby> + event<enter> = state<inner>,
             state<inner> + event<leave> = state<lobby>);
        // clang-format on
    }
};

void test_history() {
    using namespace sml;
    sm<outer> outer_sm;
    check(outer_sm.is<decltype(state<inner>)>(state<first>), "a sub-machine starts in its initial state");
    outer_sm.process_event(enter{});
    check(outer_sm.is(state<inner>), "enter moves into the sub-machine");
    check(outer_sm.process_event(step{}), "the sub-machine handles step");
    check(outer_sm.is<decltype(state<inner>)>(state<second>), "step advances the sub-machine");
    outer_sm.process_event(leave{});
    check(outer_sm.is(state<lobby>), "leave exits the sub-machine");
    outer_sm.process_event(enter{});
    check(outer_sm.is<decltype(state<inner>)>(state<second>), "history resumes the last sub-state");
    outer_sm.process_event(step{});
    check(outer_sm.is<decltype(state<inner>)>(X), "the sub-machine terminates");
    check(outer_sm.is(state<inner>), "a terminated sub-machine stays the current state");
}

// Guards combine with !, && and ||.
struct attempt {
    int value;
};

struct gate {
    auto operator()() const {
        using namespace sml;
        const auto positive = [](const attempt &e) { return e.value > 0; };
        const auto even = [](const attempt &e) { return e.value % 2 == 0; };
        const auto override_code = [](const attempt &e) { return e.value == -1; };
        return make_transition_table(*state<idle> + event<attempt>[(positive && !even) || override_code] = X);
    }
};

void test_guard_operators() {
    using namespace sml;
    // A fresh machine per value, so one accepted value cannot mask the next check.
    const auto accepts = [](int value) {
        sm<gate> gate_sm;
        return gate_sm.process_event(attempt{value}) && gate_sm.is(X);
    };
    check(!accepts(2), "! rejects an even value");
    check(!accepts(-3), "&& needs both sides");
    check(accepts(3), "positive && !even accepts an odd value");
    check(accepts(-1), "|| accepts the override");
}

// Deferred events replay after a transition; process() queues new events.
struct request {};
struct ready {};
struct reply {};

struct waiting {};
struct serving {};

struct server {
    auto operator()() const {
        using namespace sml;
        const auto took = [](journal &j) { j.trail += 'q'; };
        // clang-format off
        return make_transition_table(
            *state<waiting> + event<request> / defer,
             state<waiting> + event<ready> = state<serving>,
             state<serving> + event<request> / (took, process(reply{})),
             state<serving> + event<reply> / [](journal &j) { j.trail += 'p'; });
        // clang-format on
    }
};

void test_queues() {
    using namespace sml;
    journal j;
    sm<server, sml::defer_queue<std::deque>, sml::process_queue<std::queue>> server_sm{j};
    check(server_sm.process_event(request{}), "a deferred event counts as handled");
    server_sm.process_event(request{});
    check(server_sm.is(state<waiting>) && j.trail.empty(), "deferred events wait");
    server_sm.process_event(ready{});
    check(server_sm.is(state<serving>), "ready moves to serving");
    check(j.trail == "qqpp", "deferred events replay, then queued events run");
    server_sm.process_event(request{});
    check(j.trail == "qqppqp", "a queued event runs before process_event returns");
}

// The dispatch table maps runtime ids to event types.
struct runtime_event {
    int id;
};
struct ping {
    static constexpr auto id = 1;
    explicit ping(const runtime_event &) {}
};
struct halt {
    static constexpr auto id = 2;
};

struct pinged {};

struct dispatched {
    auto operator()() const {
        using namespace sml;
        // clang-format off
        return make_transition_table(
            *state<idle> + event<ping> = state<pinged>,
             state<pinged> + event<halt> = X);
        // clang-format on
    }
};

void test_dispatch_table() {
    using namespace sml;
    sm<dispatched> dispatched_sm;
    auto dispatch = sml::utility::make_dispatch_table<runtime_event, 1, 3>(dispatched_sm);
    check(!dispatch(runtime_event{2}, 2), "an id without a transition here is not handled");
    check(dispatch(runtime_event{1}, 1), "id 1 dispatches ping");
    check(dispatched_sm.is(state<pinged>), "ping moves the machine");
    check(!dispatch(runtime_event{3}, 3), "an id with no event is not handled");
    check(dispatch(runtime_event{2}, 2), "id 2 dispatches halt");
    check(dispatched_sm.is(X), "halt terminates the machine");
}

}  // namespace

int main() {
    test_door();
    test_testing_policy();
    test_logger();
    test_worker();
    test_history();
    test_guard_operators();
    test_queues();
    test_dispatch_table();
    if (failures != 0) {
        std::fprintf(stderr, "bext-sml (C++): %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("bext-sml (C++): ok\n");
    return 0;
}
