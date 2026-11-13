// Listeners must run as the documentation specifies: in insertion order, a prepended one first,
// a removed one no longer, and a queue's events only once processed, in FIFO order.
#include <eventpp/callbacklist.h>
#include <eventpp/eventdispatcher.h>
#include <eventpp/eventqueue.h>
#include <eventpp/hetereventdispatcher.h>
#include <eventpp/mixins/mixinfilter.h>
#include <eventpp/utilities/argumentadapter.h>
#include <eventpp/utilities/conditionalfunctor.h>
#include <eventpp/utilities/conditionalremover.h>
#include <eventpp/utilities/counterremover.h>
#include <eventpp/utilities/orderedqueuelist.h>
#include <eventpp/utilities/scopedremover.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

using Log = std::vector<int>;

void testCallbackList() {
    eventpp::CallbackList<void(int)> list;
    Log log;
    const auto a = list.append([&log](int v) { log.push_back(100 + v); });
    list.append([&log](int v) { log.push_back(200 + v); });
    list.prepend([&log](int v) { log.push_back(300 + v); });
    list.insert([&log](int v) { log.push_back(400 + v); }, a);
    list(1);
    CHECK((log == Log{301, 401, 101, 201}));

    log.clear();
    CHECK(list.remove(a));
    CHECK(!list.remove(a));
    list(2);
    CHECK((log == Log{302, 402, 202}));
    CHECK(!list.empty());
}

struct Message {
    int type;
    std::string text;
};

// The dispatcher reads the event id out of the argument instead of taking it separately.
struct MessagePolicies {
    static int getEvent(const Message &m) { return m.type; }
};

void testEventDispatcher() {
    eventpp::EventDispatcher<int, void(const std::string &, int)> dispatcher;
    std::vector<std::string> log;
    dispatcher.appendListener(3, [&log](const std::string &s, int n) { log.push_back("3:" + s + std::to_string(n)); });
    // A listener only needs a compatible prototype.
    const auto h = dispatcher.appendListener(5, [&log](std::string s, long n) { log.push_back("5a:" + s + std::to_string(n)); });
    dispatcher.appendListener(5, [&log](const std::string &s, int n) { log.push_back("5b:" + s + std::to_string(n)); });
    dispatcher.dispatch(3, "x", 1);
    dispatcher.dispatch(5, "y", 2);
    dispatcher.dispatch(7, "z", 3);
    CHECK((log == std::vector<std::string>{"3:x1", "5a:y2", "5b:y2"}));
    CHECK(dispatcher.hasAnyListener(5) && !dispatcher.hasAnyListener(7));
    CHECK(dispatcher.removeListener(5, h));
    log.clear();
    dispatcher.dispatch(5, "w", 4);
    CHECK((log == std::vector<std::string>{"5b:w4"}));

    eventpp::EventDispatcher<int, void(const Message &), MessagePolicies> byField;
    std::vector<std::string> got;
    byField.appendListener(1, [&got](const Message &m) { got.push_back("one " + m.text); });
    byField.appendListener(2, [&got](const Message &m) { got.push_back("two " + m.text); });
    byField.dispatch(Message{2, "b"});
    byField.dispatch(Message{1, "a"});
    CHECK((got == std::vector<std::string>{"two b", "one a"}));
}

void testEventQueue() {
    using Queue = eventpp::EventQueue<int, void(int)>;
    Queue queue;
    Log log;
    queue.appendListener(1, [&log](int v) { log.push_back(10 + v); });
    queue.appendListener(2, [&log](int v) { log.push_back(20 + v); });
    CHECK(queue.emptyQueue());
    CHECK(!queue.waitFor(std::chrono::milliseconds(1)));
    queue.enqueue(2, 1);
    queue.enqueue(1, 2);
    queue.enqueue(2, 3);
    CHECK(log.empty() && !queue.emptyQueue());
    CHECK(queue.waitFor(std::chrono::milliseconds(1)));

    Queue::QueuedEvent peeked{};
    CHECK(queue.peekEvent(&peeked) && peeked.event == 2 && std::get<0>(peeked.arguments) == 1);
    CHECK(queue.processOne());
    CHECK((log == Log{21}));

    Queue::QueuedEvent taken{};
    CHECK(queue.takeEvent(&taken) && taken.event == 1 && std::get<0>(taken.arguments) == 2);
    CHECK((log == Log{21}));
    queue.dispatch(taken);
    CHECK((log == Log{21, 12}));
    CHECK(queue.process());
    CHECK((log == Log{21, 12, 23}));
    CHECK(queue.emptyQueue() && !queue.process());
}

struct Job {
    int id;
    int priority;
};

// OrderedQueueList keeps the queue sorted by this comparison, highest priority first.
struct ByPriority {
    template <typename T>
    bool operator()(const T &a, const T &b) const {
        return std::get<0>(a.arguments).priority > std::get<0>(b.arguments).priority;
    }
};

struct JobPolicies {
    template <typename Item>
    using QueueList = eventpp::OrderedQueueList<Item, ByPriority>;
    static int getEvent(const Job &) { return 0; }
};

void testOrderedQueue() {
    eventpp::EventQueue<int, void(const Job &), JobPolicies> queue;
    Log log;
    queue.appendListener(0, [&log](const Job &j) { log.push_back(j.id); });
    queue.enqueue(Job{1, 5});
    queue.enqueue(Job{2, 9});
    queue.enqueue(Job{3, 1});
    queue.enqueue(Job{4, 7});
    queue.process();
    CHECK((log == Log{2, 4, 1, 3}));
}

struct FilterPolicies {
    using Mixins = eventpp::MixinList<eventpp::MixinFilter>;
};

void testFilter() {
    eventpp::EventDispatcher<int, void(int, int &), FilterPolicies> dispatcher;
    Log log;
    dispatcher.appendListener(1, [&log](int, int &v) { log.push_back(v); });
    dispatcher.appendListener(2, [&log](int, int &v) { log.push_back(-v); });
    // Filters run in order before the listeners and see the arguments by reference: the first
    // scales the caller's int, the second then blocks event 2.
    dispatcher.appendFilter([](int, int &v) -> bool { v *= 10; return true; });
    const auto block = dispatcher.appendFilter([](int e, int &) -> bool { return e != 2; });
    int a = 3, b = 4;
    dispatcher.dispatch(1, a);
    dispatcher.dispatch(2, b);
    CHECK((log == Log{30}));
    CHECK(a == 30 && b == 40);
    CHECK(dispatcher.removeFilter(block));
    dispatcher.dispatch(2, b);
    CHECK((log == Log{30, -400}));
}

void testRemovers() {
    eventpp::EventDispatcher<int, void(int)> dispatcher;
    Log log;

    // Removed after its second call.
    eventpp::counterRemover(dispatcher).appendListener(1, [&log](int v) { log.push_back(v); }, 2);
    // Removed by the call whose argument satisfies the condition; that call still reaches it.
    eventpp::conditionalRemover(dispatcher).appendListener(
        1, [&log](int v) { log.push_back(100 + v); }, [](int v) { return v == 2; });
    {
        eventpp::ScopedRemover<decltype(dispatcher)> scoped(dispatcher);
        scoped.appendListener(1, [&log](int v) { log.push_back(200 + v); });
        dispatcher.dispatch(1, 1);
    }
    dispatcher.dispatch(1, 2);
    dispatcher.dispatch(1, 3);
    CHECK((log == Log{1, 101, 201, 2, 102}));
    CHECK(!dispatcher.hasAnyListener(1));

    eventpp::CallbackList<void(int)> list;
    Log hits;
    list.append(eventpp::conditionalFunctor([&hits](int v) { hits.push_back(v); }, [](int v) { return v % 2 == 0; }));
    for (int i = 0; i < 5; ++i) {
        list(i);
    }
    CHECK((hits == Log{0, 2, 4}));
}

struct Shape {
    virtual ~Shape() {}
};
struct Square : Shape {
    explicit Square(int s) : side(s) {}
    int side;
};

void testArgumentAdapter() {
    eventpp::EventDispatcher<int, void(const Shape &)> dispatcher;
    int area = 0;
    // The listener takes the derived type; the adapter casts the base reference to it.
    dispatcher.appendListener(4, eventpp::argumentAdapter<void(const Square &)>([&area](const Square &s) { area = s.side * s.side; }));
    dispatcher.dispatch(4, Square(7));
    CHECK(area == 49);
}

enum class Kind { number, text };

struct NumberEvent {
    Kind kind() const { return Kind::number; }
    int value;
};
struct TextEvent {
    Kind kind() const { return Kind::text; }
    std::string value;
};

struct KindPolicies {
    using ArgumentPassingMode = eventpp::ArgumentPassingIncludeEvent;
    template <typename E>
    static Kind getEvent(const E &e) { return e.kind(); }
};

void testHeterDispatcher() {
    eventpp::HeterEventDispatcher<Kind, eventpp::HeterTuple<void(const NumberEvent &), void(const TextEvent &)>, KindPolicies>
        dispatcher;
    std::vector<std::string> log;
    dispatcher.appendListener(Kind::number, [&log](const NumberEvent &e) { log.push_back("n" + std::to_string(e.value)); });
    dispatcher.appendListener(Kind::text, [&log](const TextEvent &e) { log.push_back("t" + e.value); });
    dispatcher.dispatch(TextEvent{"hi"});
    dispatcher.dispatch(NumberEvent{42});
    CHECK((log == std::vector<std::string>{"thi", "n42"}));
}

}  // namespace

int main() {
    testCallbackList();
    testEventDispatcher();
    testEventQueue();
    testOrderedQueue();
    testFilter();
    testRemovers();
    testArgumentAdapter();
    testHeterDispatcher();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("eventpp: ok\n");
    return 0;
}
