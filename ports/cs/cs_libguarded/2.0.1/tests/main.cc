#include <cs_cow_guarded.h>
#include <cs_deferred_guarded.h>
#include <cs_lock_guards.h>
#include <cs_lr_guarded.h>
#include <cs_ordered_guarded.h>
#include <cs_plain_guarded.h>
#include <cs_rcu_guarded.h>
#include <cs_rcu_list.h>
#include <cs_shared_guarded.h>

#include <cstdio>
#include <shared_mutex>
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
    libguarded::plain_guarded<int> plain(0);
    {
        auto handle = plain.lock();
        *handle = 42;
    }
    check(*plain.lock() == 42, "plain_guarded value");

    libguarded::shared_guarded<std::vector<int>, std::shared_mutex> shared;
    shared.lock()->push_back(7);
    {
        auto reader = shared.lock_shared();
        check(reader->size() == 1 && reader->front() == 7, "shared_guarded contents");
    }

    libguarded::cow_guarded<std::string> cow("abc");
    const auto snapshot = cow.lock_shared();
    {
        auto handle = cow.lock();
        handle->append("d");
    }
    check(*cow.lock_shared() == "abcd", "cow_guarded commit");
    check(*snapshot == "abc", "cow_guarded snapshot");
    {
        auto handle = cow.lock();
        handle->append("e");
        handle.cancel();
    }
    check(*cow.lock_shared() == "abcd", "cow_guarded cancel");

    if (failures != 0) {
        return 1;
    }
    std::puts("cs_libguarded consumer test passed");
    return 0;
}
