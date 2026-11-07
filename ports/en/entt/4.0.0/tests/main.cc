// Consumer test (C++20): the entity-component registry (creating entities,
// recycling released identifiers with a new version, emplacing, patching,
// and removing components, views with exclusion, sorting a storage and
// sorting another one to match it, construction and destruction signals),
// entt::radix_sort, and compile-time hashed strings, through the umbrella
// header, which parses every module's headers but the Dear ImGui tools'.
#include <entt/entt.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

struct position {
  int x;
  int y;
};

struct velocity {
  int dx;
  int dy;
};

struct frozen {};

int constructed = 0;
int destroyed = 0;

void on_velocity_construct(entt::registry& registry, entt::entity entity) {
  ++constructed;
  CHECK(registry.get<velocity>(entity).dy == -1);
}

void on_velocity_destroy(entt::registry& registry, entt::entity entity) {
  ++destroyed;
  CHECK(registry.all_of<velocity>(entity));
}

}  // namespace

int main() {
  using namespace entt::literals;

  static_assert(ENTT_VERSION_MAJOR == 4 && ENTT_VERSION_MINOR == 0 && ENTT_VERSION_PATCH == 0);

  // FNV-1a over the default 32-bit identifier type.
  static_assert("foo"_hs.value() == 0xa9f37ed7u);
  CHECK(entt::hashed_string::value("position", 8) == 0x934f4e0au);

  entt::registry registry;
  registry.on_construct<velocity>().connect<&on_velocity_construct>();
  registry.on_destroy<velocity>().connect<&on_velocity_destroy>();

  std::vector<entt::entity> entities;
  for (int i = 0; i < 6; ++i) {
    const entt::entity entity = registry.create();
    registry.emplace<position>(entity, i, 10 * i);
    if (i % 2 == 0) {
      registry.emplace<velocity>(entity, 10, -1);
    }
    entities.push_back(entity);
  }
  registry.emplace<frozen>(entities[4]);
  CHECK(constructed == 3);
  CHECK(registry.storage<position>().size() == 6);
  CHECK(registry.all_of<position, velocity>(entities[0]));
  CHECK(!registry.any_of<velocity, frozen>(entities[1]));
  CHECK(registry.all_of<velocity, frozen>(entities[4]));

  // Entities with a position and a velocity, but not frozen, move.
  int moved = 0;
  registry.view<position, const velocity>(entt::exclude<frozen>)
      .each([&moved](position& pos, const velocity& vel) {
        pos.x += vel.dx;
        pos.y += vel.dy;
        ++moved;
      });
  CHECK(moved == 2);
  CHECK(registry.get<position>(entities[0]).x == 10);
  CHECK(registry.get<position>(entities[0]).y == -1);
  CHECK(registry.get<position>(entities[2]).x == 12);
  CHECK(registry.get<position>(entities[2]).y == 19);
  CHECK(registry.get<position>(entities[4]).x == 4);

  int still = 0;
  for (auto [entity, pos] : registry.view<position>(entt::exclude<velocity>).each()) {
    CHECK(entity == entities[static_cast<std::size_t>(pos.x)]);
    ++still;
  }
  CHECK(still == 3);

  registry.patch<position>(entities[1], [](position& pos) { pos.x = 100; });
  CHECK(registry.try_get<velocity>(entities[1]) == nullptr);
  CHECK(registry.get_or_emplace<velocity>(entities[1], 0, -1).dy == -1);
  CHECK(constructed == 4);

  // Sorting a storage orders its iteration, and views led by it.  Then
  // velocity follows position's order.
  registry.sort<position>([](const position& lhs, const position& rhs) { return lhs.x > rhs.x; });
  std::vector<int> xs;
  for (auto [entity, pos] : registry.view<position>().each()) {
    xs.push_back(pos.x);
  }
  CHECK(xs == std::vector<int>{100, 12, 10, 5, 4, 3});

  registry.sort<velocity, position>();
  const auto velocities = registry.view<velocity>();
  std::vector<entt::entity> order(velocities.begin(), velocities.end());
  CHECK(order == std::vector<entt::entity>{entities[1], entities[2], entities[0], entities[4]});

  // A destroyed entity loses its components; the next create() recycles
  // its index with the next version.
  const entt::entity old = entities[2];
  CHECK(registry.destroy(old) == entt::to_version(old) + 1u);
  CHECK(destroyed == 1);
  CHECK(!registry.valid(old));
  const entt::entity recycled = registry.create();
  CHECK(registry.valid(recycled));
  CHECK(entt::to_entity(recycled) == entt::to_entity(old));
  CHECK(entt::to_version(recycled) == entt::to_version(old) + 1u);
  CHECK(!registry.any_of<position, velocity>(recycled));
  CHECK(registry.storage<position>().size() == 5);

  CHECK(registry.remove<velocity>(entities[0]) == 1u);
  CHECK(registry.remove<velocity>(entities[0]) == 0u);
  CHECK(destroyed == 2);

  registry.clear();
  CHECK(destroyed == 4);
  CHECK(registry.storage<position>().empty());
  CHECK(!registry.valid(entities[0]));

  // LSD radix sort, eight bits per pass, over all 32 bits of each key; every
  // byte decides some pair's order.
  std::vector<std::uint32_t> keys{0x01000000u, 0x00ff0000u, 0x0100u, 0xffu,
                                  0x01020304u, 0x04030201u, 0u, 0xffffffffu};
  entt::radix_sort<8, 32>{}(keys.begin(), keys.end());
  CHECK(keys == std::vector<std::uint32_t>{0u, 0xffu, 0x0100u, 0x00ff0000u, 0x01000000u,
                                           0x01020304u, 0x04030201u, 0xffffffffu});

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("entt consumer test passed");
  return 0;
}
