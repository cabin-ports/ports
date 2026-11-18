// C++98-compatible: the probe builds this at the declared c++98.  Every bound below follows from the
// timers reading one monotonic clock, so none depends on scheduling: a timer started earlier, or read
// later, never reports less elapsed time.  Direct reads of that clock pin the absolute scale.
#include <plf_nanotimer.h>

#ifdef PLF_NANO_NOEXCEPT
#error "plf_nanotimer.h leaves PLF_NANO_NOEXCEPT defined"
#endif

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

// The unit conversions round, so compare with a relative margin far below one nanosecond.
bool atLeast(double value, double bound) { return value >= bound * (1.0 - 1e-12); }

// Direct reads of the clock each platform's nanotimer uses (plf_nanotimer.h's three implementations).
#if defined(__MACH__)
typedef mach_timespec_t Stamp;
class Clock {
    clock_serv_t service;

public:
    Clock() { host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &service); }
    ~Clock() { mach_port_deallocate(mach_task_self(), service); }
    Stamp now() const {
        Stamp s;
        clock_get_time(service, &s);
        return s;
    }
};
#elif defined(_WIN32)
typedef LARGE_INTEGER Stamp;
class Clock {
public:
    Stamp now() const {
        Stamp s;
        QueryPerformanceCounter(&s);
        return s;
    }
};
#else
typedef timespec Stamp;
class Clock {
public:
    Stamp now() const {
        Stamp s;
        clock_gettime(CLOCK_MONOTONIC, &s);
        return s;
    }
};
#endif

double msBetween(const Stamp &a, const Stamp &b) {
#if defined(_WIN32) && !defined(__MACH__)
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
#else
    return (1e9 * static_cast<double>(b.tv_sec - a.tv_sec) + static_cast<double>(b.tv_nsec - a.tv_nsec)) / 1e6;
#endif
}

// Waits until the clock's whole-second field moves past from's, so that an interval spanning the
// wait exercises both fields of a reading.  QueryPerformanceCounter has a single field.
void crossSecond(const Clock &direct, const Stamp &from) {
#if defined(_WIN32) && !defined(__MACH__)
    (void)direct;
    (void)from;
#else
    while (direct.now().tv_sec == from.tv_sec) {
    }
#endif
}

}  // namespace

int main() {
    plf::nanotimer outer;
    outer.start();

    // The delays busy-wait on their own timer, started after t, until it reaches the delay.
    plf::nanotimer t;
    t.start();
    plf::millisecond_delay(2.0);
    const double ms = t.get_elapsed_ms();
    CHECK(atLeast(ms, 2.0));

    // Successive reads are ordered, which pins each unit's scale against the others.
    const double ns1 = t.get_elapsed_ns();
    const double us = t.get_elapsed_us();
    const double ms2 = t.get_elapsed_ms();
    const double ns2 = t.get_elapsed_ns();
    CHECK(atLeast(ns1, ms * 1000000.0));
    CHECK(atLeast(us * 1000.0, ns1));
    CHECK(atLeast(ms2 * 1000000.0, us * 1000.0));
    CHECK(atLeast(ns2, ms2 * 1000000.0));

    t.start();
    plf::microsecond_delay(250.0);
    CHECK(atLeast(t.get_elapsed_us(), 250.0));
    t.start();
    plf::nanosecond_delay(40000.0);
    CHECK(atLeast(t.get_elapsed_ns(), 40000.0));

    // Restarting resets the origin: outer, started before the delays above, is ahead of the restarted t
    // by at least their total.
    t.start();
    const double sinceRestart = t.get_elapsed_ms();
    const double sinceOuter = outer.get_elapsed_ms();
    CHECK(sinceRestart >= 0.0);
    CHECK(atLeast(sinceOuter, sinceRestart + 2.0 + 0.25 + 0.04));

    // A reading lies between direct reads taken just inside and just outside the timed interval.
    Clock direct;
    const Stamp before = direct.now();
    plf::nanotimer timed;
    timed.start();
    const Stamp startedAfter = direct.now();
    crossSecond(direct, startedAfter);
    plf::millisecond_delay(1.0);
    const Stamp readBefore = direct.now();
    const double reading = timed.get_elapsed_ms();
    const Stamp after = direct.now();
    CHECK(atLeast(reading, msBetween(startedAfter, readBefore)));
    CHECK(atLeast(msBetween(before, after), reading));

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("plf-nanotimer: ok\n");
    return 0;
}
