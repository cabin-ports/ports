#include <concurrencpp/concurrencpp.h>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

using pool_ptr = std::shared_ptr<concurrencpp::thread_pool_executor>;

concurrencpp::result<int> twice_plus_one(pool_ptr pool, int n) {
    const int doubled = co_await pool->submit([n] { return n * 2; });
    co_return doubled + 1;
}

concurrencpp::result<int> chained(pool_ptr pool) {
    const int a = co_await twice_plus_one(pool, 20);  // 41
    const int b = co_await twice_plus_one(pool, a);   // 83
    co_return b;
}

concurrencpp::result<int> sum_of_squares(pool_ptr pool) {
    std::vector<concurrencpp::result<int>> parts;
    for (int i = 1; i <= 4; ++i) {
        parts.emplace_back(pool->submit([i] { return i * i; }));
    }
    auto done = co_await concurrencpp::when_all(pool, parts.begin(), parts.end());
    int sum = 0;
    for (auto& part : done) {
        sum += co_await part;
    }
    co_return sum;
}

concurrencpp::result<std::string> mixed(pool_ptr pool) {
    auto [number, text] = co_await concurrencpp::when_all(
        pool, pool->submit([] { return 7; }), pool->submit([] { return std::string("seven"); }));
    const int n = co_await number;
    const std::string t = co_await text;
    co_return std::to_string(n) + "=" + t;
}

concurrencpp::result<int> rethrows(pool_ptr pool) {
    co_return co_await pool->submit([]() -> int { throw std::runtime_error("boom"); });
}

}  // namespace

int main() {
    concurrencpp::runtime runtime;

    check(concurrencpp::runtime::version() == std::make_tuple(0u, 1u, 7u), "runtime::version() is 0.1.7");

    auto pool = runtime.thread_pool_executor();
    check(pool->submit([] { return 6 * 7; }).get() == 42, "thread pool submit returns 42");

    auto inline_result = runtime.inline_executor()->submit([] { return 5; });
    check(inline_result.status() == concurrencpp::result_status::value, "inline executor runs the task at submit");
    check(inline_result.get() == 5, "inline executor result is 5");

    check(runtime.thread_executor()->submit([] { return 11; }).get() == 11, "thread executor result is 11");

    auto worker = runtime.make_worker_thread_executor();
    check(worker->submit([] { return 12; }).get() == 12, "worker thread executor result is 12");

    auto manual = runtime.make_manual_executor();
    auto queued = manual->submit([] { return std::string("queued"); });
    check(manual->size() == 1, "manual executor holds one task");
    check(queued.status() == concurrencpp::result_status::idle, "queued task has not run");
    check(manual->loop_once(), "loop_once runs a task");
    check(manual->empty(), "manual executor is drained");
    check(queued.get() == "queued", "manual executor result");

    check(chained(pool).get() == 83, "coroutine chain co_awaits results");
    check(sum_of_squares(pool).get() == 30, "when_all over a range");
    check(mixed(pool).get() == "7=seven", "when_all over a tuple");

    check(concurrencpp::make_ready_result<int>(9).get() == 9, "make_ready_result");

    auto failed = pool->submit([]() -> int { throw std::runtime_error("direct"); });
    failed.wait();
    check(failed.status() == concurrencpp::result_status::exception, "failed task status is exception");
    try {
        failed.get();
        check(false, "get() rethrows the task's exception");
    } catch (const std::runtime_error& e) {
        check(std::string(e.what()) == "direct", "rethrown exception message");
    }

    try {
        rethrows(pool).get();
        check(false, "a coroutine propagates an awaited exception");
    } catch (const std::runtime_error& e) {
        check(std::string(e.what()) == "boom", "propagated exception message");
    }

    if (failures != 0) {
        return 1;
    }
    std::puts("concurrencpp ok");
    return 0;
}
