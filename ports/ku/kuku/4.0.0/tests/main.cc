#include "kuku/kuku.h"

#include <cstdio>
#include <cstdlib>
#include <set>

using namespace kuku;

#define CHECK(c)                                                                  \
  do {                                                                            \
    if (!(c)) {                                                                   \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c);  \
      std::exit(1);                                                               \
    }                                                                             \
  } while (0)

int main() {
  const item_type seed = make_item(0x0123456789abcdefULL, 0xfedcba9876543210ULL);

  // A roomy table: every item is placed at one of its own location-function
  // positions, and absent items are not found.
  {
    KukuTable t(1024, 0, 3, seed, 100, make_zero_item());
    CHECK(t.table_size() == 1024 && t.stash_size() == 0 && t.loc_func_count() == 3);
    CHECK(t.loc_func_seed() == seed && t.max_probe() == 100);
    constexpr std::uint64_t n = 300;
    for (std::uint64_t i = 1; i <= n; i++) {
      CHECK(t.insert(make_item(i, 7)));
    }
    CHECK(t.fill_rate() == static_cast<double>(n) / 1024);
    for (std::uint64_t i = 1; i <= n; i++) {
      const item_type item = make_item(i, 7);
      QueryResult q = t.query(item);
      CHECK(q.found() && !q.in_stash());
      CHECK(q.loc_func_index() < 3);
      CHECK(t.table(q.location()) == item);
      CHECK(t.location(item, q.loc_func_index()) == q.location());
      CHECK(t.all_locations(item).count(q.location()) == 1);
      CHECK(*q.location_if_found() == q.location());
    }
    for (std::uint64_t i = 1; i <= n; i++) {
      QueryResult q = t.query(make_item(i, 8));
      CHECK(!q && !q.location_if_found());
      CHECK(q.loc_func_index() == max_loc_func_count);
    }
    CHECK(!t.insert(make_item(5, 7)));  // already present

    // The location functions are a pure function of the seed.
    KukuTable u(1024, 0, 3, seed, 1, make_zero_item());
    for (std::uint32_t k = 0; k < 3; k++) {
      CHECK(u.location(make_item(42, 0), k) == t.location(make_item(42, 0), k));
    }
    t.clear_table();
    CHECK(t.fill_rate() == 0.0 && !t.query(make_item(1, 7)));
  }

  // One slot, one location function and a stash of two: the first item sits in
  // the table whichever way the walk goes, two more fit in the stash, the
  // fourth fails and is reported as the leftover.
  {
    KukuTable t(1, 2, 1, seed, 10, make_zero_item());
    const item_type a = make_item(1, 1), b = make_item(2, 1), c = make_item(3, 1), d = make_item(4, 1);
    CHECK(t.insert(a) && t.insert(b) && t.insert(c));
    CHECK(t.fill_rate() == 1.0);
    CHECK(!t.insert(d));
    CHECK(t.leftover_item() == d && !t.query(d));
    CHECK(t.fill_rate() == 1.0);
    int in_table = 0, in_stash = 0;
    for (const item_type &x : {a, b, c}) {
      QueryResult q = t.query(x);
      CHECK(q.found());
      if (q.in_stash()) {
        in_stash++;
        CHECK(t.stash(q.location()) == x);
      } else {
        in_table++;
        CHECK(q.location() == 0 && t.table(0) == x);
      }
    }
    CHECK(in_table == 1 && in_stash == 2 && t.stash().size() == 2);
  }

  // Argument validation.
  {
    bool threw = false;
    try {
      KukuTable bad(0, 0, 2, seed, 1, make_zero_item());
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    CHECK(threw);
    KukuTable t(8, 0, 2, seed, 10, make_zero_item());
    threw = false;
    try {
      (void)t.insert(make_zero_item());
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    CHECK(threw);
  }

  std::puts("kuku ok");
  return 0;
}
