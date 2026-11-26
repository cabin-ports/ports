#include <signals_light/signal.hpp>

#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);              \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main()
{
    // Empty signal.
    sl::Signal<int(int)> sig;
    CHECK(sig.is_empty());
    CHECK(sig.slot_count() == 0);
    CHECK(sig(4) == std::nullopt);

    // Result of the last connected slot; every slot runs.
    int calls = 0;
    auto const first = sig.connect([&](int i) { ++calls; return i * 2; });
    auto const second = sig.connect([&](int i) { ++calls; return i + 1; });
    CHECK(first != second);
    CHECK(sig.slot_count() == 2);
    CHECK(!sig.is_empty());
    CHECK(sig(4) == std::optional<int>{5});
    CHECK(sig.emit(10) == std::optional<int>{11});
    CHECK(calls == 4);

    // Disconnect returns the slot and leaves the other one.
    auto removed = sig.disconnect(second);
    CHECK(removed.slot_function()(7) == 8);
    CHECK(sig.slot_count() == 1);
    CHECK(sig(4) == std::optional<int>{8});
    bool threw = false;
    try {
        sig.disconnect(second);
    }
    catch (std::invalid_argument const&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(sig.slot_count() == 1);
    sig.disconnect(first);
    CHECK(sig.is_empty());
    CHECK(sig(4) == std::nullopt);

    // Void signal: every slot runs, in connection order.
    sl::Signal<void(std::string const&)> vsig;
    std::vector<std::string> log;
    vsig.connect([&](std::string const& s) { log.push_back("a" + s); });
    vsig.connect([&](std::string const& s) { log.push_back("b" + s); });
    vsig.connect([&](std::string const& s) { log.push_back("c" + s); });
    vsig("1");
    vsig.emit("2");
    CHECK((log == std::vector<std::string>{"a1", "b1", "c1", "a2", "b2", "c2"}));

    // Lifetime tracking: an expired slot is skipped, and the result is the
    // last non-expired slot's.
    sl::Signal<int()> tsig;
    tsig.connect([] { return 1; });
    auto life = std::make_unique<sl::Lifetime>();
    sl::Slot<int()> tracked{[] { return 2; }};
    tracked.track(*life);
    CHECK(!tracked.is_expired());
    CHECK(tracked() == 2);
    tsig.connect(tracked);
    CHECK(tsig() == std::optional<int>{2});
    life.reset();
    CHECK(tracked.is_expired());
    bool expired = false;
    try {
        tracked();
    }
    catch (sl::Slot<int()>::Expired const&) {
        expired = true;
    }
    CHECK(expired);
    CHECK(tsig.slot_count() == 2);
    CHECK(tsig() == std::optional<int>{1});

    // All slots expired: nullopt.
    auto sp = std::make_shared<int>(0);
    sl::Slot<int()> only{[] { return 3; }};
    only.track(sp);
    sl::Signal<int()> osig{only};
    CHECK(osig() == std::optional<int>{3});
    sp.reset();
    CHECK(osig() == std::nullopt);

    // Tracking can be removed again.
    auto l2 = std::make_unique<sl::Lifetime>();
    sl::Slot<int()> u{[] { return 4; }};
    u.track(*l2);
    u.untrack(*l2);
    l2.reset();
    CHECK(!u.is_expired());
    CHECK(u() == 4);

    // Identifier from a fresh signal starts equal to a default one.
    sl::Signal<void()> idsig;
    CHECK(idsig.connect([] {}) == sl::Identifier{});

    return failures == 0 ? 0 : 1;
}
