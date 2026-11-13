// The HFSM2_ENABLE_* switches are the consumer's; this test turns on four.
#define HFSM2_ENABLE_LOG_INTERFACE
#define HFSM2_ENABLE_PLANS
#define HFSM2_ENABLE_SERIALIZATION
#define HFSM2_ENABLE_TRANSITION_HISTORY
#include <hfsm2/machine.hpp>

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

namespace light {

struct Context {
    bool powerOn;
    int greens;
};

using Config = hfsm2::Config::ContextT<Context &>;
using M = hfsm2::MachineT<Config>;

#define S(s) struct s
using FSM = M::PeerRoot<S(Off), M::Composite<S(On), S(Red), S(Yellow), S(Green)>, S(Done)>;
#undef S

struct Go {};

struct Off : FSM::State {
    void entryGuard(FullControl &control)
    {
        if (control.context().powerOn) {
            control.changeTo<On>();
        }
    }
};

struct On : FSM::State {
    void enter(PlanControl &control)
    {
        auto plan = control.plan();
        plan.change<Red, Yellow>();
        plan.change<Yellow, Green>();
        plan.change<Green, Yellow>();
        plan.change<Yellow, Red>();
    }

    void planSucceeded(FullControl &control)
    {
        control.changeTo<Done>();
    }
};

struct Red : FSM::State {
    void update(FullControl &control)
    {
        control.succeed();
    }
};

struct Yellow : FSM::State {
    void update(FullControl &control)
    {
        control.succeed();
    }
};

// Green has no update(), so only the Go event completes its plan step.
struct Green : FSM::State {
    void enter(PlanControl &control)
    {
        ++control.context().greens;
    }

    void react(const Go &, EventControl &control)
    {
        control.succeed();
    }
};

struct Done : FSM::State {};

void test()
{
    Context context;
    context.powerOn = false;
    context.greens = 0;
    {
        FSM::Instance off{context};
        check(off.isActive<Off>() && !off.isActive<On>(), "light: without power Off stays active");
    }

    context.powerOn = true;
    FSM::Instance fsm{context};
    check(fsm.isActive<On>() && fsm.isActive<Red>() && !fsm.isActive<Off>(),
          "light: Off's entryGuard reroutes the initial activation into On");
    fsm.update();
    check(fsm.isActive<Yellow>() && !fsm.isActive<Red>(), "light: the plan's first step");
    fsm.update();
    check(fsm.isActive<Green>() && context.greens == 1, "light: the plan's second step enters Green");
    fsm.update();
    check(fsm.isActive<Green>(), "light: update() does not complete Green's step");
    fsm.react(Go{});
    check(fsm.isActive<Yellow>(), "light: the Go event completes Green's step");
    fsm.update();
    check(fsm.isActive<Red>(), "light: the plan's last step");
    fsm.update();
    check(fsm.isActive<Done>() && !fsm.isActive<On>() && context.greens == 1,
          "light: planSucceeded() leaves On for Done");
}

} // namespace light

namespace service {

struct Context {
    int lampsLit;
};

using Config = hfsm2::Config::ContextT<Context &>;
using M = hfsm2::MachineT<Config>;

#define S(s) struct s
using FSM = M::PeerRoot<M::Composite<S(Idle), S(First), S(Second)>,
                        M::Orthogonal<S(Service), S(Lamp), S(Diagnostics)>,
                        S(Halted)>;
#undef S

struct Fault {};

struct Idle : FSM::State {};
struct First : FSM::State {};
struct Second : FSM::State {};
struct Service : FSM::State {};

struct Lamp : FSM::State {
    void enter(PlanControl &control)
    {
        ++control.context().lampsLit;
    }

    void exit(PlanControl &control)
    {
        --control.context().lampsLit;
    }
};

struct Diagnostics : FSM::State {
    void react(const Fault &, EventControl &control)
    {
        control.changeTo<Halted>();
    }
};

struct Halted : FSM::State {};

// Without HFSM2_ENABLE_VERBOSE_DEBUG_LOG, enter() is reported only for states
// that define it, here Lamp.
struct Logger : Config::LoggerInterface {
    std::vector<StateID> entered;
    std::vector<TransitionType> transitions;

    void recordMethod(const Context &, const StateID origin, const Method method) override
    {
        if (method == Method::ENTER) {
            entered.push_back(origin);
        }
    }

    void recordTransition(const Context &, const StateID, const TransitionType type, const StateID) override
    {
        transitions.push_back(type);
    }
};

void test()
{
    Context context;
    context.lampsLit = 0;
    Logger logger;
    FSM::Instance fsm{context, &logger};
    check(fsm.isActive<Idle>() && fsm.isActive<First>() && !fsm.isActive<Service>(),
          "service: the first region and its first state start active");

    fsm.immediateChangeTo<Second>();
    check(fsm.isActive<Second>() && !fsm.isActive<First>(), "service: immediateChangeTo() within a region");
    check(fsm.previousTransitions().count() == 1 &&
              fsm.previousTransitions()[0].destination == FSM::stateId<Second>() &&
              fsm.previousTransitions()[0].type == hfsm2::TransitionType::CHANGE,
          "service: transition history records the external change");

    fsm.immediateChangeTo<Service>();
    check(fsm.isActive<Service>() && fsm.isActive<Lamp>() && fsm.isActive<Diagnostics>() && !fsm.isActive<Idle>(),
          "service: an orthogonal region activates every sub-state");
    check(context.lampsLit == 1 && fsm.isResumable<Second>(), "service: Lamp entered, Second kept for resume");

    fsm.react(Fault{});
    check(fsm.isActive<Halted>() && !fsm.isActive<Service>() && context.lampsLit == 0,
          "service: Diagnostics' event handler leaves the orthogonal region");
    check(fsm.previousTransitions().count() == 1 &&
              fsm.previousTransitions()[0].origin == FSM::stateId<Diagnostics>() &&
              fsm.previousTransitions()[0].destination == FSM::stateId<Halted>(),
          "service: transition history records the internal change and its origin");

    fsm.immediateResume<Idle>();
    check(fsm.isActive<Second>() && !fsm.isActive<First>(), "service: resume() returns to the last active sub-state");

    FSM::Instance::SerialBuffer buffer;
    fsm.save(buffer);
    Context other_context;
    other_context.lampsLit = 0;
    FSM::Instance other{other_context};
    check(other.isActive<First>(), "service: a fresh instance starts at First");
    other.load(buffer);
    check(other.isActive<Idle>() && other.isActive<Second>() && !other.isActive<First>() &&
              !other.isActive<Halted>(),
          "service: load() restores the saved configuration");
    check(fsm.isResumable<Halted>() && other.isResumable<Halted>(), "service: load() restores the history as well");

    fsm.immediateRestart<Idle>();
    check(fsm.isActive<First>() && !fsm.isActive<Second>(), "service: restart() ignores history");

    const hfsm2::StateID lamp = FSM::stateId<Lamp>();
    check(logger.entered.size() == 1 && logger.entered[0] == lamp, "service: the logger saw Lamp's enter() once");
    const hfsm2::TransitionType expected[] = {hfsm2::TransitionType::CHANGE, hfsm2::TransitionType::CHANGE,
                                              hfsm2::TransitionType::CHANGE, hfsm2::TransitionType::RESUME,
                                              hfsm2::TransitionType::RESTART};
    check(logger.transitions == std::vector<hfsm2::TransitionType>(expected, expected + 5),
          "service: the logger saw every transition request in order");
}

} // namespace service

int main()
{
    light::test();
    service::test();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("hfsm2 2.12.1 ok");
    return 0;
}
