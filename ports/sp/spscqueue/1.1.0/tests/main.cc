// Drives rigtorp::SPSCQueue through its API on one thread, with a custom
// allocator, and with one producer and one consumer thread.  The capacity,
// lifetime, noexcept and overflow cases are modeled on upstream's
// src/SPSCQueueTest.cpp.
#include <rigtorp/SPSCQueue.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <thread>
#include <utility>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Tracked {
    static int live;
    int value;
    explicit Tracked(int v) noexcept : value(v) { ++live; }
    Tracked(const Tracked &other) noexcept : value(other.value) { ++live; }
    ~Tracked() noexcept { --live; }
};
int Tracked::live = 0;

struct Point {
    int x;
    int y;
    Point(int x_, int y_) noexcept : x(x_), y(y_) {}
};

// Copy-only: an rvalue must fall back to push(const T &).
struct ThrowingCopy {
    ThrowingCopy() {}
    ThrowingCopy(const ThrowingCopy &) {}
    ThrowingCopy(ThrowingCopy &&) = delete;
};

struct NothrowCopy {
    NothrowCopy() noexcept {}
    NothrowCopy(const NothrowCopy &) noexcept {}
};

// push and emplace are noexcept exactly when constructing the element is.
typedef rigtorp::SPSCQueue<ThrowingCopy> ThrowingQueue;
typedef rigtorp::SPSCQueue<NothrowCopy> NothrowQueue;
typedef rigtorp::SPSCQueue<std::unique_ptr<int>> OwningQueue;
static_assert(!noexcept(std::declval<ThrowingQueue &>().push(std::declval<const ThrowingCopy &>())),
              "push of a throwing copy is not noexcept");
static_assert(!noexcept(std::declval<ThrowingQueue &>().try_push(std::declval<ThrowingCopy>())),
              "try_push of a throwing copy is not noexcept");
static_assert(!noexcept(std::declval<ThrowingQueue &>().emplace(std::declval<const ThrowingCopy &>())),
              "emplace of a throwing copy is not noexcept");
static_assert(noexcept(std::declval<NothrowQueue &>().push(std::declval<const NothrowCopy &>())),
              "push of a nothrow copy is noexcept");
static_assert(noexcept(std::declval<NothrowQueue &>().try_push(std::declval<const NothrowCopy &>())),
              "try_push of a nothrow copy is noexcept");
static_assert(noexcept(std::declval<OwningQueue &>().push(std::declval<std::unique_ptr<int>>())),
              "push of a moved unique_ptr is noexcept");
static_assert(noexcept(std::declval<OwningQueue &>().emplace(std::declval<std::unique_ptr<int>>())),
              "emplace of a moved unique_ptr is noexcept");

void test_single_thread()
{
    rigtorp::SPSCQueue<int> q(2);
    check(q.capacity() == 2 && q.size() == 0 && q.empty() && q.front() == nullptr,
          "a new queue is empty");
    check(q.try_push(1), "try_push into an empty queue");
    q.push(2);
    check(q.size() == 2 && !q.empty(), "two elements fill a queue of capacity 2");
    check(!q.try_push(3) && !q.try_emplace(3) && q.size() == 2, "try_push and try_emplace fail on a full queue");
    check(q.front() != nullptr && *q.front() == 1, "front is the first element");
    q.pop();
    check(q.size() == 1 && q.front() != nullptr && *q.front() == 2, "pop removes the first element");
    q.pop();
    check(q.front() == nullptr && q.empty(), "the queue is empty again");

    // 200 elements through the 4 slots of a queue of capacity 3.
    rigtorp::SPSCQueue<int> wrap(3);
    bool wrapped = true;
    for (int i = 0; i < 100; ++i) {
        wrap.push(i);
        wrap.push(i + 1000);
        wrapped = wrapped && wrap.size() == 2 && *wrap.front() == i;
        wrap.pop();
        wrapped = wrapped && *wrap.front() == i + 1000;
        wrap.pop();
    }
    check(wrapped && wrap.empty(), "push and pop wrap around the slots");

    rigtorp::SPSCQueue<int> one(0);
    check(one.capacity() == 1 && one.try_push(7) && !one.try_push(8), "a capacity of zero becomes one");

    rigtorp::SPSCQueue<Point> points(2);
    points.emplace(1, 2);
    check(points.try_emplace(3, 4) && !points.try_emplace(5, 6), "emplace and try_emplace construct in place");
    check(points.front()->x == 1 && points.front()->y == 2, "the first emplaced element");
    points.pop();
    check(points.front()->x == 3 && points.front()->y == 4, "the second emplaced element");
    points.pop();

    ThrowingQueue copies(2);
    copies.push(ThrowingCopy());
    check(copies.try_push(ThrowingCopy()) && copies.size() == 2, "rvalues of a copy-only type are copied in");
}

void test_lifetimes()
{
    {
        const Tracked third(3);
        rigtorp::SPSCQueue<Tracked> q(3);
        q.emplace(1);
        q.push(Tracked(2));
        check(q.try_push(third), "try_push copies a const lvalue");
        check(Tracked::live == 4 && q.size() == 3, "three elements live in the queue");
        check(!q.try_push(third) && !q.try_emplace(9) && Tracked::live == 4,
              "a failed try_push or try_emplace constructs nothing");
        check(q.front()->value == 1, "front is the emplaced element");
        q.pop();
        check(Tracked::live == 3 && q.front()->value == 2, "pop destroys the front element");
        q.push(third);
        check(Tracked::live == 4 && q.size() == 3, "push copies a const lvalue");
    }
    check(Tracked::live == 0, "the queue destroys the elements left in it");

    rigtorp::SPSCQueue<std::unique_ptr<int>> owned(2);
    owned.push(std::unique_ptr<int>(new int(7)));
    std::unique_ptr<int> next(new int(8));
    check(owned.try_push(std::move(next)) && !next, "a move-only element is moved in");
    std::unique_ptr<int> out = std::move(*owned.front());
    owned.pop();
    check(out && *out == 7, "a move-only element is moved out");
    check(owned.front() != nullptr && **owned.front() == 8, "the second move-only element");

    bool threw = false;
    try {
        rigtorp::SPSCQueue<int> huge(SIZE_MAX - 1);
    } catch (const std::exception &) { // bad_alloc, or length_error before libc++ 14
        threw = true;
    }
    check(threw, "a capacity whose padding would overflow size_t fails to allocate");
}

struct AllocatorLog {
    int allocations;
    int deallocations;
    std::size_t allocated;
    std::size_t deallocated;
};

template <typename T> struct LoggingAllocator {
    typedef T value_type;
    AllocatorLog *log;
    explicit LoggingAllocator(AllocatorLog *l) noexcept : log(l) {}
    T *allocate(std::size_t n)
    {
        ++log->allocations;
        log->allocated = n;
        return std::allocator<T>().allocate(n);
    }
    void deallocate(T *p, std::size_t n)
    {
        ++log->deallocations;
        log->deallocated = n;
        std::allocator<T>().deallocate(p, n);
    }
};

void test_allocator()
{
    AllocatorLog log = {0, 0, 0, 0};
    {
        rigtorp::SPSCQueue<int, LoggingAllocator<int>> q(5, LoggingAllocator<int>(&log));
        // capacity + 1 slots, plus at least a cache line of padding on each side.
        const std::size_t padding = log.allocated - 6;
        check(log.allocations == 1 && log.allocated > 6 && padding % 2 == 0 &&
                  padding / 2 * sizeof(int) >= 64,
              "the queue allocates its padded slots once through its allocator");
        q.push(9);
        check(q.capacity() == 5 && *q.front() == 9 && log.deallocations == 0,
              "a queue with a custom allocator passes elements");
        q.pop();
    }
    check(log.deallocations == 1 && log.deallocated == log.allocated, "the destructor deallocates the slots");
}

// One producer pushes 0..count-1, alternating the try_ and the blocking
// paths, through a queue much smaller than count; the consumer must see
// every value once and in order whatever the interleaving.
void test_two_threads()
{
    const std::uint32_t count = 100000;
    rigtorp::SPSCQueue<std::uint32_t> q(16);
    std::thread producer([&] {
        for (std::uint32_t i = 0; i < count; ++i) {
            if (i % 2 == 0) {
                while (!q.try_push(i)) {
                    std::this_thread::yield();
                }
            } else {
                q.push(i);
            }
        }
    });
    bool in_order = true;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t *value;
        while ((value = q.front()) == nullptr) {
            std::this_thread::yield();
        }
        in_order = in_order && *value == i;
        q.pop();
    }
    producer.join();
    check(in_order, "every value arrives once and in order");
    check(q.empty() && q.front() == nullptr, "the queue is empty after the transfer");
}

} // namespace

int main()
{
    test_single_thread();
    test_lifetimes();
    test_allocator();
    test_two_threads();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("spscqueue 1.1.0 ok");
    return 0;
}
