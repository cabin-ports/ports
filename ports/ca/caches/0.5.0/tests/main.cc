#include <caches/queues/fifo_cache.tpp>
#include <caches/queues/lifo_cache.tpp>
#include <caches/stores/lru_cache.tpp>
#include <caches/stores/store_cache.tpp>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    caches::FIFOCache<std::string> fifo(2);
    fifo.push("a");
    fifo.push("b");
    fifo.push("c");
    check(fifo.size() == 2, "fifo size");
    check(fifo.space() == 0, "fifo space");
    check(fifo.next() == "b", "fifo next");
    check(fifo.last() == "c", "fifo last");
    check(fifo.slice(1) == std::vector<std::string>{"c"}, "fifo slice");

    caches::LIFOCache<std::string> lifo(2);
    lifo.push("a");
    lifo.push("b");
    lifo.push("c");
    check(lifo.size() == 2, "lifo size");
    check(lifo.next() == "c", "lifo next");
    check(lifo.last() == "b", "lifo last");
    check(lifo.space() == 0, "lifo space");
    check(lifo.slice(1) == std::vector<std::string>{"c"}, "lifo slice");

    caches::LRUCache<std::string, int> lru(2);
    lru.store("a", 1);
    lru.store("b", 2);
    lru.store("c", 3);
    check(lru.size() == 2, "lru size");
    check(lru["b"] == 2, "lru b");
    check(lru["c"] == 3, "lru c");
    check(!lru.contains("a"), "lru evicted a");
    bool threw = false;
    try {
        lru["a"];
    } catch (const std::range_error &) {
        threw = true;
    }
    check(threw, "lru missing key throws range_error");
    // Reading "b" makes "c" the least recently used, so storing "d" evicts it.
    check(lru["b"] == 2, "lru b again");
    lru.store("d", 4);
    check(lru.contains("b") && !lru.contains("c") && lru["d"] == 4, "lru recency");
    lru.store("b", 5);
    check(lru.size() == 2 && lru["b"] == 5 && lru.contains("d"), "lru restore");

    caches::StoreCache<std::string, std::unique_ptr<int>> store(2);
    store.store("a", std::unique_ptr<int>(new int(1)));
    store.store("b", std::unique_ptr<int>(new int(2)));
    store.store("c", std::unique_ptr<int>(new int(3)));
    check(store.size() == 2, "store size");
    check(*store.at("b") == 2, "store b");
    check(*store.at("c") == 3, "store c");
    check(!store.contains("a"), "store evicted a");
    check(*store.at("b") == 2, "store b again");
    store.store("d", std::unique_ptr<int>(new int(4)));
    check(store.contains("b") && !store.contains("c"), "store recency");

    if (failures != 0) {
        return 1;
    }
    std::puts("caches consumer test passed");
    return 0;
}
