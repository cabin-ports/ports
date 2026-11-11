// Both pools hand out resources that go back to the pool on release, through
// the recycle function, and are destroyed instead once the pool is gone.
// Copies allocate their own unused resources, moves and assignments keep them,
// and the locking policy is taken on allocation, recycling and counting.
#include <recycle/shared_pool.hpp>
#include <recycle/unique_pool.hpp>

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

int constructed = 0;
int destroyed = 0;

struct buffer {
  explicit buffer(int number) : id(number) { ++constructed; }
  ~buffer() { ++destroyed; }
  int id;
  std::vector<char> bytes;
};

struct node : std::enable_shared_from_this<node> {
  node() { ++constructed; }
  ~node() { ++destroyed; }
};

int locks = 0;

struct counting_policy {
  struct mutex_type {};
  struct lock_type {
    explicit lock_type(mutex_type&) { ++locks; }
  };
};

struct std_policy {
  using mutex_type = std::mutex;
  using lock_type = std::lock_guard<std::mutex>;
};

struct no_default {
  explicit no_default(int) {}
};

static_assert(std::is_default_constructible<recycle::shared_pool<std::string>>::value, "");
static_assert(!std::is_default_constructible<recycle::shared_pool<no_default>>::value, "");
static_assert(!std::is_default_constructible<recycle::unique_pool<no_default>>::value, "");

void shared_pool_recycles() {
  constructed = destroyed = 0;
  int next_id = 0;
  std::vector<int> recycled;
  recycle::shared_pool<buffer> pool(
      [&next_id] { return std::make_shared<buffer>(++next_id); },
      [&recycled](std::shared_ptr<buffer> resource) {
        recycled.push_back(resource->id);
        resource->bytes.clear();
      });

  std::shared_ptr<buffer> first = pool.allocate();
  first->bytes.assign(3, 'x');
  const buffer* const address = first.get();
  CHECK(first->id == 1);
  CHECK(pool.unused_resources() == 0);
  first.reset();
  CHECK(recycled == std::vector<int>{1});
  CHECK(pool.unused_resources() == 1);

  std::shared_ptr<buffer> again = pool.allocate();
  CHECK(again.get() == address);
  CHECK(again->bytes.empty());
  std::shared_ptr<buffer> second = pool.allocate();
  CHECK(second->id == 2);
  CHECK(constructed == 2);
  // A weak_ptr outliving the release must not keep the resource from being
  // freed (shared_pool.hpp:322-377).
  const std::weak_ptr<buffer> watcher = again;
  again.reset();
  CHECK(watcher.expired());
  second.reset();
  CHECK((recycled == std::vector<int>{1, 1, 2}));
  CHECK(pool.unused_resources() == 2);

  // A copy allocates as many fresh resources as the original holds unused.
  recycle::shared_pool<buffer> copy(pool);
  CHECK(copy.unused_resources() == 2);
  CHECK(constructed == 4);
  CHECK(copy.allocate()->id == 4);
  copy.free_unused();
  CHECK(destroyed == 2);
  CHECK(pool.unused_resources() == 2);
  pool.free_unused();
  CHECK(destroyed == 4);

  std::shared_ptr<buffer> kept;
  {
    recycle::shared_pool<buffer> scoped([] { return std::make_shared<buffer>(9); });
    kept = scoped.allocate();
  }
  CHECK(kept->id == 9);
  CHECK(destroyed == 4);
  kept.reset();
  CHECK(destroyed == 5);
}

void shared_pool_policies() {
  constructed = destroyed = 0;
  {
    recycle::shared_pool<node> pool;
    std::shared_ptr<node> value = pool.allocate();
    CHECK(value->shared_from_this().get() == value.get());
    value.reset();
    CHECK(pool.unused_resources() == 1);
  }
  CHECK(constructed == 1);
  CHECK(destroyed == 1);

  locks = 0;
  recycle::shared_pool<int, counting_policy> counted;
  std::shared_ptr<int> number = counted.allocate();
  CHECK(*number == 0);
  number.reset();
  CHECK(counted.unused_resources() == 1);
  CHECK(locks == 3);

  recycle::shared_pool<int, std_policy> guarded;
  guarded.allocate();
  CHECK(guarded.unused_resources() == 1);
  recycle::shared_pool<int, std_policy> assigned([] { return std::make_shared<int>(5); });
  assigned = guarded;
  CHECK(assigned.unused_resources() == 1);
  CHECK(*assigned.allocate() == 0);
  recycle::shared_pool<int, std_policy> moved(std::move(guarded));
  CHECK(moved.unused_resources() == 1);
}

void unique_pool_recycles() {
  constructed = destroyed = 0;
  int next_id = 0;
  int recycled = 0;
  recycle::unique_pool<buffer> pool(
      [&next_id] { return std::make_unique<buffer>(++next_id); },
      [&recycled](std::unique_ptr<buffer>& resource) {
        ++recycled;
        resource->bytes.clear();
      });

  {
    recycle::unique_pool<buffer>::pool_ptr first = pool.allocate();
    first->bytes.assign(5, 'y');
    CHECK(first->id == 1);
  }
  CHECK(recycled == 1);
  CHECK(pool.unused_resources() == 1);

  recycle::unique_pool<buffer>::pool_ptr reused = pool.allocate();
  CHECK(reused->id == 1);
  CHECK(reused->bytes.empty());
  CHECK(constructed == 1);
  recycle::unique_pool<buffer>::pool_ptr moved = std::move(reused);
  CHECK(!reused);
  moved.reset();
  CHECK(recycled == 2);
  CHECK(pool.unused_resources() == 1);
  {
    recycle::unique_pool<buffer> twin(pool);
    CHECK(twin.unused_resources() == 1);
    CHECK(twin.allocate()->id == 2);
  }
  CHECK(destroyed == 1);
  pool.free_unused();
  CHECK(destroyed == 2);

  {
    recycle::unique_pool<buffer>::pool_ptr kept;
    {
      recycle::unique_pool<buffer> scoped([] { return std::make_unique<buffer>(9); });
      kept = scoped.allocate();
    }
    CHECK(kept->id == 9);
    CHECK(destroyed == 2);
  }
  // The resource lives in the deleter, so it goes with the pointer itself.
  CHECK(destroyed == 3);

  recycle::unique_pool<std::string> strings;
  CHECK(strings.allocate()->empty());
  CHECK(strings.unused_resources() == 1);
}

}  // namespace

int main() {
  shared_pool_recycles();
  shared_pool_policies();
  unique_pool_recycles();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("recycle C++ consumer test passed");
  return 0;
}
