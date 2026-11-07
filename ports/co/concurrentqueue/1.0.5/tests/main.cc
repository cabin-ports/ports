// The C++ consumer: the lock-free ConcurrentQueue on one thread (FIFO,
// producer and consumer tokens, bulk operations, a move-only element type)
// and under four producers and four consumers, the BlockingConcurrentQueue's
// waits, and the LightweightSemaphore under it.
#include <moodycamel/blockingconcurrentqueue.h>
#include <moodycamel/concurrentqueue.h>
#include <moodycamel/lightweightsemaphore.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

void one_thread() {
    moodycamel::ConcurrentQueue<int> queue;
    for (int i = 0; i < 100; i++) {
        queue.enqueue(i);
    }
    check(queue.size_approx() == 100, "100 elements queued");
    bool in_order = true;
    int item = -1;
    for (int i = 0; i < 100; i++) {
        in_order = queue.try_dequeue(item) && item == i && in_order;
    }
    check(in_order, "one producer's elements come out in order");
    check(!queue.try_dequeue(item), "an empty queue yields nothing");

    // Each token is its own producer; its elements keep their order.
    moodycamel::ProducerToken first(queue), second(queue);
    const int lows[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const int highs[] = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    check(queue.enqueue_bulk(first, lows, 10) && queue.enqueue_bulk(second, highs, 10),
          "bulk enqueue through tokens");
    check(queue.try_dequeue_from_producer(second, item) && item == 100,
          "dequeue from a chosen producer");

    moodycamel::ConsumerToken consumer(queue);
    int out[32];
    const std::size_t count = queue.try_dequeue_bulk(consumer, out, 32);
    check(count == 19, "bulk dequeue takes all 19 left");
    std::vector<int> low_seen, high_seen;
    for (std::size_t i = 0; i < count; i++) {
        (out[i] < 100 ? low_seen : high_seen).push_back(out[i]);
    }
    check(low_seen == std::vector<int>(lows, lows + 10), "the first producer's ten, in order");
    check(high_seen == std::vector<int>(highs + 1, highs + 10),
          "the second producer's other nine, in order");

    moodycamel::ConcurrentQueue<std::unique_ptr<int>> owners;
    owners.enqueue(std::unique_ptr<int>(new int(7)));
    std::unique_ptr<int> owned;
    check(owners.try_dequeue(owned) && owned && *owned == 7, "a move-only element moves through");
}

// Four producers enqueue 20000 tagged values each, through their own tokens,
// while four consumers drain the queue in bulk.  Every value must come out
// exactly once, and each consumer must see each producer's values in the
// order that producer enqueued them.  Consumers stop once all values are
// in, or 30 seconds after starting if the producers are done and values are
// missing, so a lost value fails the test instead of hanging it.
const int producers = 4, consumers = 4, per_producer = 20000, total = producers * per_producer;

void many_threads() {
    moodycamel::ConcurrentQueue<int> queue;
    std::atomic<int> consumed(0), producing(producers);
    std::atomic<bool> enqueued(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::vector<std::vector<int>> received(consumers);
    bool ordered[consumers] = {true, true, true, true};

    std::vector<std::thread> threads;
    for (int p = 0; p < producers; p++) {
        threads.emplace_back([&queue, &producing, &enqueued, p] {
            moodycamel::ProducerToken token(queue);
            for (int i = 0; i < per_producer; i++) {
                if (!queue.enqueue(token, p * 1000000 + i)) {
                    enqueued = false;
                }
            }
            producing.fetch_sub(1);
        });
    }
    for (int c = 0; c < consumers; c++) {
        threads.emplace_back([&, c] {
            moodycamel::ConsumerToken token(queue);
            int last[producers] = {-1, -1, -1, -1};
            int buffer[64];
            while (consumed.load() < total) {
                // Read before dequeuing, so an empty read counts only after every enqueue.
                const bool produced = producing.load() == 0;
                const std::size_t count = queue.try_dequeue_bulk(token, buffer, 64);
                if (count == 0) {
                    if (produced && std::chrono::steady_clock::now() > deadline) {
                        break;
                    }
                    std::this_thread::yield();
                    continue;
                }
                for (std::size_t i = 0; i < count; i++) {
                    const int p = buffer[i] / 1000000, seq = buffer[i] % 1000000;
                    if (p < 0 || p >= producers || seq <= last[p]) {
                        ordered[c] = false;
                    } else {
                        last[p] = seq;
                    }
                    received[c].push_back(buffer[i]);
                }
                consumed.fetch_add(static_cast<int>(count));
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    std::vector<int> all;
    for (int c = 0; c < consumers; c++) {
        check(ordered[c], "a consumer sees each producer's values in order");
        all.insert(all.end(), received[c].begin(), received[c].end());
    }
    std::sort(all.begin(), all.end());
    std::vector<int> expected;
    for (int p = 0; p < producers; p++) {
        for (int i = 0; i < per_producer; i++) {
            expected.push_back(p * 1000000 + i);
        }
    }
    std::printf("concurrentqueue: %zu of %d values received\n", all.size(), total);
    check(enqueued, "every enqueue succeeds");
    check(all == expected, "every value comes out exactly once");
    check(queue.size_approx() == 0, "the queue ends empty");
}

void blocking() {
    moodycamel::BlockingConcurrentQueue<int> queue;
    int item = 0;
    check(!queue.wait_dequeue_timed(item, std::chrono::milliseconds(5)),
          "waiting on an empty queue times out");

    std::thread producer([&queue] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        queue.enqueue(42);
    });
    const bool arrived = queue.wait_dequeue_timed(item, std::chrono::seconds(10));
    producer.join();
    check(arrived && item == 42, "a wait blocks until an element arrives");

    const int five[] = {1, 2, 3, 4, 5};
    queue.enqueue_bulk(five, 5);
    int out[8];
    check(queue.wait_dequeue_bulk_timed(out, 8, std::chrono::seconds(10)) == 5 &&
              std::equal(five, five + 5, out),
          "a bulk wait takes what is there");
    check(!queue.wait_dequeue_timed(item, 1000), "and the queue is empty again");
}

void semaphore() {
    moodycamel::LightweightSemaphore semaphore(2);
    check(semaphore.tryWait() && semaphore.tryWait() && !semaphore.tryWait(),
          "a semaphore of 2 admits two");
    semaphore.signal(3);
    check(semaphore.availableApprox() == 3, "signal(3) makes three available");
    check(semaphore.tryWaitMany(5) == 3, "tryWaitMany takes the three");
    check(!semaphore.wait(1000), "a timed wait on zero times out");
}

}  // namespace

int main() {
    one_thread();
    many_threads();
    blocking();
    semaphore();
    std::printf("concurrentqueue: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
