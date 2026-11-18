#include <ctrack.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

static unsigned work(unsigned n) {
    CTRACK_NAME("work");
    // ctrack.hpp:582-583 skips the statistics, thread count included, when
    // every duration is zero, which a coarse clock could record.
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    unsigned sum = 0;
    for (unsigned i = 1; i <= n; ++i)
        sum += i;
    return sum;
}

int main() {
    unsigned total = 0;
    for (unsigned i = 1; i <= 10; ++i)
        total += work(i);
    check(total == 220, "work's results");

    // Each result call sleeps 100 ms and clears the recorded events.
    const auto tables = ctrack::result_get_tables();
    check(tables.summary.rows.size() == 1, "summary rows");
    check(tables.details.rows.size() == 1, "detail rows");
    if (tables.summary.rows.size() != 1 || tables.details.rows.size() != 1)
        return 1;

    const auto& summary = tables.summary.rows[0];
    check(summary.function_name == "work", "summary function name");
    check(summary.calls == 10, "summary calls");
    const auto& detail = tables.details.rows[0];
    check(detail.function_name == "work", "detail function name");
    check(detail.calls == 10, "detail calls");
    check(detail.threads == 1, "detail threads");
    check(detail.time_acc >= std::chrono::milliseconds(1), "detail accumulated time");

    if (failures != 0)
        return 1;
    std::puts("ctrack consumer test passed");
    return 0;
}
