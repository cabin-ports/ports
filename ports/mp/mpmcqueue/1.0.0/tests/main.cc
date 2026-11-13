// Drives rigtorp::MPMCQueue through its API, with a custom allocator, and
// under two producers and two consumers.  The lifetime, capacity and
// contention cases are modeled on upstream's src/MPMCQueueTest.cpp.
#include <rigtorp/MPMCQueue.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>
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

// Counts live objects.  The 129-byte payload, as in upstream's test, makes a
// slot span more than one cache line.
struct Tracked {
    static int live;
    int value;
    char payload[129];
    Tracked() noexcept : value(-1) { ++live; }
    explicit Tracked(int v) noexcept : value(v) { ++live; }
    Tracked(const Tracked &other) noexcept : value(other.value) { ++live; }
    Tracked &operator=(const Tracked &other) noexcept
    {
        value = other.value;
        return *this;
    }
    ~Tracked() noexcept { --live; }
};
int Tracked::live = 0;

struct Point {
    int x;
    int y;
    Point() noexcept : x(0), y(0) {}
    Point(int x_, int y_) noexcept : x(x_), y(y_) {}
};

void test_single_thread()
{
    rigtorp::MPMCQueue<int> q(4);
    check(q.size() == 0 && q.empty(), "a new queue is empty");
    bool pushed = true;
    for (int i = 0; i < 4; ++i) {
        pushed = q.try_push(i) && pushed;
    }
    check(pushed && q.size() == 4 && !q.empty(), "try_push fills the capacity");
    check(!q.try_push(4) && q.size() == 4, "try_push fails on a full queue");
    int v = -1;
    bool in_order = true;
    for (int i = 0; i < 4; ++i) {
        in_order = q.try_pop(v) && v == i && in_order;
    }
    check(in_order, "elements come out in FIFO order");
    check(!q.try_pop(v) && v == 3 && q.empty(), "try_pop fails on an empty queue and leaves its argument");

    // 200 tickets through 4 slots.
    bool wrapped = true;
    for (int i = 0; i < 100; ++i) {
        int a = -1;
        int b = -1;
        q.push(i);
        q.push(i + 1000);
        q.pop(a);
        q.pop(b);
        wrapped = wrapped && a == i && b == i + 1000;
    }
    check(wrapped && q.size() == 0, "push and pop wrap around the slots");

    rigtorp::MPMCQueue<Point> points(2);
    points.emplace(1, 2);
    check(points.try_emplace(3, 4) && !points.try_emplace(5, 6), "emplace and try_emplace construct in place");
    Point p;
    points.pop(p);
    check(p.x == 1 && p.y == 2, "the first emplaced element");
    check(points.try_pop(p) && p.x == 3 && p.y == 4, "the second emplaced element");
}

void test_lifetimes()
{
    {
        rigtorp::MPMCQueue<Tracked> q(3);
        q.emplace(1);
        q.push(Tracked(2));
        const Tracked third(3);
        check(q.try_push(third), "try_push copies a const lvalue");
        check(Tracked::live == 4 && q.size() == 3, "three elements live in the queue");
        Tracked out;
        q.pop(out);
        check(out.value == 1 && Tracked::live == 4, "pop assigns and destroys the slot's element");
    }
    check(Tracked::live == 0, "the queue destroys the elements left in it");

    rigtorp::MPMCQueue<std::unique_ptr<int>> owned(2);
    owned.push(std::unique_ptr<int>(new int(7)));
    std::unique_ptr<int> next(new int(8));
    check(owned.try_push(std::move(next)) && !next, "a move-only element is moved in");
    std::unique_ptr<int> out;
    owned.pop(out);
    check(out && *out == 7, "a move-only element is moved out");
    check(owned.try_pop(out) && out && *out == 8, "try_pop moves a move-only element out");

    bool threw = false;
    try {
        rigtorp::MPMCQueue<int> none(0);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    check(threw, "a capacity of zero throws std::invalid_argument");
}

struct AllocatorLog {
    int allocations;
    int deallocations;
    std::size_t allocated;
    std::size_t deallocated;
};

// A stateful allocator that logs its calls and allocates through the header's
// default allocator, which provides the slots' cache-line alignment.
template <typename T> struct LoggingAllocator {
    typedef T value_type;
    AllocatorLog *log;
    explicit LoggingAllocator(AllocatorLog *l) noexcept : log(l) {}
    T *allocate(std::size_t n)
    {
        ++log->allocations;
        log->allocated = n;
        return rigtorp::mpmc::AlignedAllocator<T>().allocate(n);
    }
    void deallocate(T *p, std::size_t n)
    {
        ++log->deallocations;
        log->deallocated = n;
        rigtorp::mpmc::AlignedAllocator<T>().deallocate(p, n);
    }
};

void test_allocator()
{
    typedef LoggingAllocator<rigtorp::mpmc::Slot<int>> Allocator;
    AllocatorLog log = {0, 0, 0, 0};
    {
        rigtorp::MPMCQueue<int, Allocator> q(5, Allocator(&log));
        check(log.allocations == 1 && log.allocated == 6, "the queue allocates capacity + 1 slots through its allocator");
        int v = 0;
        q.push(9);
        q.pop(v);
        check(v == 9 && log.deallocations == 0, "a queue with a custom allocator passes elements");
    }
    check(log.deallocations == 1 && log.deallocated == 6, "the destructor deallocates the slots");
}

void test_waiting_reader()
{
    rigtorp::MPMCQueue<int> q(2);
    int got = 0;
    std::thread reader([&] { q.pop(got); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (q.size() != -1 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    check(q.size() == -1 && q.empty(), "a waiting reader makes the size negative");
    q.push(42);
    reader.join();
    check(got == 42 && q.size() == 0, "push releases the waiting reader");
}

// Each value is producer << 24 | sequence.  Every value must arrive exactly
// once, and each consumer must see each producer's values in order, since
// tickets are taken in order on both sides.
void test_contention(bool blocking)
{
    const unsigned producers = 2;
    const unsigned consumers = 2;
    const std::uint32_t per_producer = 5000;
    const std::uint32_t per_consumer = producers * per_producer / consumers;
    rigtorp::MPMCQueue<std::uint32_t> q(8);
    std::atomic<bool> go(false);
    std::vector<std::vector<std::uint32_t>> received(consumers);
    std::vector<int> ordered(consumers, 1);
    std::vector<std::thread> threads;

    for (unsigned p = 0; p < producers; ++p) {
        threads.push_back(std::thread([&, p] {
            while (!go) {
                std::this_thread::yield();
            }
            for (std::uint32_t seq = 0; seq < per_producer; ++seq) {
                const std::uint32_t value = (static_cast<std::uint32_t>(p) << 24) | seq;
                if (blocking) {
                    q.push(value);
                } else {
                    while (!q.try_push(value)) {
                        std::this_thread::yield();
                    }
                }
            }
        }));
    }
    for (unsigned c = 0; c < consumers; ++c) {
        threads.push_back(std::thread([&, c] {
            std::vector<std::int64_t> last(producers, -1);
            std::vector<std::uint32_t> &mine = received[c];
            mine.reserve(per_consumer);
            while (!go) {
                std::this_thread::yield();
            }
            for (std::uint32_t i = 0; i < per_consumer; ++i) {
                std::uint32_t value = 0;
                if (blocking) {
                    q.pop(value);
                } else {
                    while (!q.try_pop(value)) {
                        std::this_thread::yield();
                    }
                }
                const std::uint32_t p = value >> 24;
                const std::int64_t seq = value & 0xffffff;
                if (p >= producers || seq <= last[p]) {
                    ordered[c] = 0;
                } else {
                    last[p] = seq;
                }
                mine.push_back(value);
            }
        }));
    }
    go = true;
    for (std::size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    std::vector<unsigned char> seen(producers * per_producer, 0);
    bool exactly_once = true;
    bool in_order = true;
    for (unsigned c = 0; c < consumers; ++c) {
        in_order = in_order && ordered[c] == 1;
        for (std::size_t i = 0; i < received[c].size(); ++i) {
            const std::uint32_t value = received[c][i];
            const std::uint32_t p = value >> 24;
            const std::uint32_t seq = value & 0xffffff;
            const std::size_t index = static_cast<std::size_t>(p) * per_producer + seq;
            if (p >= producers || seq >= per_producer || seen[index] != 0) {
                exactly_once = false;
            } else {
                seen[index] = 1;
            }
        }
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
        exactly_once = exactly_once && seen[i] == 1;
    }
    check(exactly_once, blocking ? "push/pop: every value arrives exactly once"
                                 : "try_push/try_pop: every value arrives exactly once");
    check(in_order, blocking ? "push/pop: each producer's values arrive in order"
                             : "try_push/try_pop: each producer's values arrive in order");
    check(q.size() == 0 && q.empty(), "the queue is empty after the threads are joined");
}

} // namespace

int main()
{
    test_single_thread();
    test_lifetimes();
    test_allocator();
    test_waiting_reader();
    test_contention(true);
    test_contention(false);
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("mpmcqueue 1.0.0 ok");
    return 0;
}
