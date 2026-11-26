#include <anax/anax.hpp>

#include <cstdio>
#include <type_traits>

namespace {

struct Position : anax::Component {
  Position(int x_, int y_) : x(x_), y(y_) {}
  int x, y;
};
struct Velocity : anax::Component {
  explicit Velocity(int dx_) : dx(dx_) {}
  int dx;
};
struct Frozen : anax::Component {};

struct MoveSystem
    : anax::System<anax::Requires<Position, Velocity>, anax::Excludes<Frozen>> {
  int added = 0, removed = 0;
  void onEntityAdded(anax::Entity &) override { ++added; }
  void onEntityRemoved(anax::Entity &) override { ++removed; }
  void step() {
    for (auto &e : getEntities()) {
      e.getComponent<Position>().x += e.getComponent<Velocity>().dx;
    }
  }
};

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

} // namespace

int main() {
  // Config.hpp carries upstream's CMake defaults (patch 0001).
  check(anax::DEFAULT_ENTITY_POOL_SIZE == 1000, "default pool size");
  check(anax::MAX_AMOUNT_OF_COMPONENTS == 64, "max components");
  check(std::is_polymorphic<anax::Component>::value, "virtual component dtor");

  anax::World world;
  MoveSystem moves;
  world.addSystem(moves);
  check(world.doesSystemExist<MoveSystem>(), "system registered");

  anax::Entity a = world.createEntity();
  anax::Entity b = world.createEntity();
  anax::Entity c = world.createEntity();
  a.addComponent<Position>(1, 2);
  a.addComponent<Velocity>(10);
  b.addComponent<Position>(5, 5);
  c.addComponent<Position>(0, 0);
  c.addComponent<Velocity>(7);
  c.addComponent<Frozen>();
  a.activate();
  b.activate();
  c.activate();
  world.refresh();

  check(world.getEntityCount() == 3, "entity count");
  check(moves.getEntities().size() == 1, "only a matches Requires/Excludes");
  check(moves.added == 1, "onEntityAdded");
  check(a.hasComponent<Velocity>() && !b.hasComponent<Velocity>(),
        "hasComponent");

  moves.step();
  moves.step();
  check(a.getComponent<Position>().x == 21, "system moved a twice");
  check(a.getComponent<Position>().y == 2, "y untouched");
  check(c.getComponent<Position>().x == 0, "excluded entity unmoved");

  // Removing the excluded component makes c match.
  c.removeComponent<Frozen>();
  c.activate();
  world.refresh();
  check(moves.getEntities().size() == 2 && moves.added == 2, "c joins system");

  a.deactivate();
  world.refresh();
  check(moves.getEntities().size() == 1 && moves.removed == 1,
        "deactivate leaves system");
  check(!world.isActivated(a) && a.isValid(), "deactivated but valid");

  b.kill();
  world.refresh();
  check(!b.isValid() && world.getEntityCount() == 2, "kill invalidates");
  check(a.getComponent<Position>().x == 21, "survivor intact");

  world.removeSystem<MoveSystem>();
  check(!world.doesSystemExist<MoveSystem>(), "system removed");

  world.clear();
  check(world.getEntityCount() == 0, "clear");

  if (failures == 0)
    std::printf("anax ok\n");
  return failures == 0 ? 0 : 1;
}
