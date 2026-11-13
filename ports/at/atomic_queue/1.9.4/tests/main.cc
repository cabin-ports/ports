// Drives the fixed-size and the allocated queues, for atomic and for
// non-atomic elements, on one thread, then in SPSC mode and under two
// producers and two consumers started through barrier.h's Barrier.  On Linux
// it also drives the pthread spinlock queue, which builds only there.
#include <atomic_queue/atomic_queue.h>
#include <atomic_queue/barrier.h>
#if defined(__linux__)
#include <atomic_queue/atomic_queue_mutex.h>
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace aq = atomic_queue;

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The single-threaded contract of any of the queues, with values 1..capacity
// (0 is the default NIL of the atomic queues and must not be pushed).
template <class Queue> void check_fifo(Queue &q, unsigned capacity, const char *name)
{
    bool ok = q.was_empty() && !q.was_full() && q.was_size() == 0;
    for (unsigned i = 1; i <= capacity; ++i) {
        ok = q.try_push(i) && ok;
    }
    ok = ok && q.was_full() && q.was_size() == capacity && !q.try_push(capacity + 1);
    for (unsigned i = 1; i <= capacity; ++i) {
        unsigned v = 0;
        ok = q.try_pop(v) && v == i && ok;
    }
    unsigned v = 0;
    ok = ok && !q.try_pop(v) && q.was_empty();
    // Blocking calls across many laps of the ring buffer.
    for (unsigned i = 1; i <= 10 * capacity; ++i) {
        q.push(i);
        ok = q.pop() == i && ok;
    }
    check(ok, name);
}

void test_single_thread()
{
    aq::AtomicQueue<unsigned, 8> fixed;
    check(fixed.capacity() == 8, "AtomicQueue: capacity");
    check_fifo(fixed, 8, "AtomicQueue: FIFO, full and empty");

    aq::AtomicQueue2<unsigned, 5> rounded;
    check(rounded.capacity() == 8, "AtomicQueue2: capacity rounds up to a power of 2");
    check_fifo(rounded, 8, "AtomicQueue2: FIFO, full and empty");

    aq::AtomicQueue<unsigned, 5, 0, false> exact;
    check(exact.capacity() == 5, "AtomicQueue without MINIMIZE_CONTENTION keeps its capacity");
    check_fifo(exact, 5, "AtomicQueue without MINIMIZE_CONTENTION: FIFO");

    // The allocated queues round a small capacity up to the square of the
    // slots per cache line, which depends on the target.
    aq::AtomicQueueB<unsigned> allocated(10);
    const unsigned per_line = aq::CACHE_LINE_SIZE / sizeof(std::atomic<unsigned>);
    check(allocated.capacity() == per_line * per_line, "AtomicQueueB: capacity rounds up");
    check_fifo(allocated, per_line * per_line, "AtomicQueueB: FIFO, full and empty");

    // A NIL other than 0 lets 0 through.
    aq::AtomicQueue<int, 4, -1> minus_one_nil;
    check(minus_one_nil.try_push(0) && minus_one_nil.pop() == 0, "AtomicQueue with NIL -1 passes 0");

    // Move-only and non-trivial elements.
    aq::AtomicQueue2<std::unique_ptr<int>, 4> owned;
    std::unique_ptr<int> one(new int(1));
    check(owned.try_push(std::move(one)) && !one, "AtomicQueue2 moves a unique_ptr in");
    owned.push(std::unique_ptr<int>(new int(2)));
    std::unique_ptr<int> out;
    check(owned.try_pop(out) && out && *out == 1, "AtomicQueue2 moves a unique_ptr out");
    out = owned.pop();
    check(out && *out == 2 && owned.was_empty(), "AtomicQueue2 pop returns the element");

    aq::AtomicQueueB2<std::string> strings(4);
    check(strings.try_push(std::string(100, 'x')) && strings.try_push(std::string("short")),
          "AtomicQueueB2 takes strings");
    std::string s;
    check(strings.try_pop(s) && s == std::string(100, 'x') && strings.pop() == "short",
          "AtomicQueueB2 returns them in order");

    // The allocated queues move and swap.
    aq::AtomicQueueB2<std::string> moved(std::move(strings));
    check(moved.try_push(std::string("kept")) && strings.capacity() == 0, "AtomicQueueB2 moves");
    aq::AtomicQueueB2<std::string> other(4);
    swap(moved, other);
    check(other.was_size() == 1 && moved.was_empty() && other.pop() == "kept", "AtomicQueueB2 swaps");
}

// One producer and one consumer through an SPSC queue: every value in order.
void test_spsc()
{
    const unsigned count = 20000;
    aq::AtomicQueue<unsigned, 1024, 0, true, true, false, true> q;
    check(q.is_spsc(), "the SPSC queue reports SPSC mode");
    bool in_order = true;
    std::thread consumer([&] {
        for (unsigned i = 1; i <= count; ++i) {
            in_order = q.pop() == i && in_order;
        }
    });
    for (unsigned i = 1; i <= count; ++i) {
        q.push(i);
    }
    consumer.join();
    check(in_order && q.was_empty(), "SPSC: every value arrives in order");

    // Non-atomic elements written by one thread and read by the other.
    aq::AtomicQueue2<std::string, 16, true, true, false, true> strings;
    bool strings_in_order = true;
    std::thread reader([&] {
        for (unsigned i = 0; i < 2000; ++i) {
            strings_in_order = strings.pop() == std::to_string(i) + std::string(40, '.') && strings_in_order;
        }
    });
    for (unsigned i = 0; i < 2000; ++i) {
        strings.push(std::to_string(i) + std::string(40, '.'));
    }
    reader.join();
    check(strings_in_order && strings.was_empty(), "SPSC AtomicQueue2: every string arrives intact and in order");
}

// Each value is (producer + 1) << 24 | sequence, never NIL, and must arrive
// exactly once.  Order across threads is not checked: a producer a lap ahead
// can fill a slot before the producer whose ticket it is.  The 4000 values wrap
// the fixed 64-slot rings many times.
template <class Queue> void check_contention(Queue &q, bool blocking, const char *name)
{
    const unsigned producers = 2;
    const unsigned consumers = 2;
    const unsigned per_producer = 2000;
    const unsigned per_consumer = producers * per_producer / consumers;
    aq::Barrier barrier;
    std::vector<std::vector<unsigned>> received(consumers);
    std::vector<std::thread> threads;

    for (unsigned p = 0; p < producers; ++p) {
        threads.push_back(std::thread([&, p] {
            barrier.wait();
            for (unsigned seq = 0; seq < per_producer; ++seq) {
                const unsigned v = ((p + 1) << 24) | seq;
                if (blocking) {
                    q.push(v);
                } else {
                    while (!q.try_push(v)) {
                        std::this_thread::yield();
                    }
                }
            }
        }));
    }
    for (unsigned c = 0; c < consumers; ++c) {
        threads.push_back(std::thread([&, c] {
            received[c].reserve(per_consumer);
            barrier.wait();
            for (unsigned i = 0; i < per_consumer; ++i) {
                unsigned v = 0;
                if (blocking) {
                    v = q.pop();
                } else {
                    while (!q.try_pop(v)) {
                        std::this_thread::yield();
                    }
                }
                received[c].push_back(v);
            }
        }));
    }
    barrier.release(static_cast<int>(producers + consumers));
    for (std::size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    std::vector<unsigned char> seen(producers * per_producer, 0);
    bool exactly_once = true;
    for (unsigned c = 0; c < consumers; ++c) {
        for (std::size_t i = 0; i < received[c].size(); ++i) {
            const unsigned p = (received[c][i] >> 24) - 1;
            const unsigned seq = received[c][i] & 0xffffff;
            if (p >= producers || seq >= per_producer || seen[p * per_producer + seq] != 0) {
                exactly_once = false;
            } else {
                seen[p * per_producer + seq] = 1;
            }
        }
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
        exactly_once = exactly_once && seen[i] == 1;
    }
    check(exactly_once && q.was_empty(), name);
}

void test_contention()
{
    aq::AtomicQueue<unsigned, 64> atomic_slots;
    check_contention(atomic_slots, true, "AtomicQueue: 2x2 threads, push/pop, exactly once");
    check_contention(atomic_slots, false, "AtomicQueue: 2x2 threads, try_push/try_pop, exactly once");
    aq::AtomicQueue2<unsigned, 64> state_slots;
    check_contention(state_slots, true, "AtomicQueue2: 2x2 threads, push/pop, exactly once");
    check_contention(state_slots, false, "AtomicQueue2: 2x2 threads, try_push/try_pop, exactly once");
    aq::AtomicQueueB2<unsigned> allocated(64);
    check_contention(allocated, true, "AtomicQueueB2: 2x2 threads, push/pop, exactly once");
    check_contention(allocated, false, "AtomicQueueB2: 2x2 threads, try_push/try_pop, exactly once");
}

#if defined(__linux__)
// The lock-based queues have only try_push and try_pop; their was_empty() and
// was_full() lock a non-mutable mutex in a const function and do not compile.
template <class Queue> void check_try_fifo(Queue &q, unsigned capacity, const char *name)
{
    bool ok = true;
    for (unsigned i = 1; i <= capacity; ++i) {
        ok = q.try_push(i) && ok;
    }
    ok = ok && !q.try_push(capacity + 1);
    for (unsigned i = 1; i <= capacity; ++i) {
        unsigned v = 0;
        ok = q.try_pop(v) && v == i && ok;
    }
    unsigned v = 0;
    check(ok && !q.try_pop(v), name);
}

void test_mutex_queues()
{
    aq::AtomicQueueSpinlock<unsigned, 8> spinlocked;
    check_try_fifo(spinlocked, 8, "AtomicQueueSpinlock: FIFO, full and empty");
    aq::AtomicQueueMutex<unsigned, 8, std::mutex> locked;
    check_try_fifo(locked, 8, "AtomicQueueMutex with std::mutex: FIFO, full and empty");
}
#endif

} // namespace

int main()
{
    test_single_thread();
    test_spsc();
    test_contention();
#if defined(__linux__)
    test_mutex_queues();
#endif
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("atomic_queue 1.9.4 ok");
    return 0;
}
