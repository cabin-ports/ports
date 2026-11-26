// physac.h's own bool enum clashes with the stdbool that Apple's headers pull in
// later; use the system bool and tell the header so.
#include <stdbool.h>
#ifndef _STDBOOL_H
#define _STDBOOL_H
#endif

#define PHYSAC_STANDALONE
#define PHYSAC_NO_THREADS
#define PHYSAC_IMPLEMENTATION
#include <physac.h>

#include <stdio.h>
#include <time.h>

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                  \
    }                                                            \
  } while (0)

int main(void) {
  InitPhysics();
  CHECK(GetPhysicsBodiesCount() == 0);

  PhysicsBody floor = CreatePhysicsBodyRectangle((Vector2){100.0f, 200.0f}, 400.0f, 20.0f, 1.0f);
  floor->enabled = false;  // static: no dynamics, still collides
  PhysicsBody circle = CreatePhysicsBodyCircle((Vector2){100.0f, 10.0f}, 5.0f, 1.0f);

  CHECK(GetPhysicsBodiesCount() == 2);
  CHECK(GetPhysicsBody(0) == floor && GetPhysicsBody(1) == circle);
  CHECK(GetPhysicsShapeType(0) == PHYSICS_POLYGON);
  CHECK(GetPhysicsShapeType(1) == PHYSICS_CIRCLE);
  CHECK(GetPhysicsShapeVerticesCount(0) == 4);
  CHECK(GetPhysicsShapeVerticesCount(1) == PHYSAC_CIRCLE_VERTICES);
  CHECK(floor->position.x == 100.0f && floor->position.y == 200.0f);
  CHECK(circle->position.x == 100.0f && circle->position.y == 10.0f);

  // RunPhysicsStep() advances by elapsed wall-clock time, so spin until the
  // circle has fallen under the default gravity (+y), with a deadline.
  SetPhysicsGravity(0.0f, 9.81f);
  clock_t deadline = clock() + 5 * CLOCKS_PER_SEC;
  while (circle->position.y == 10.0f && clock() < deadline) RunPhysicsStep();
  CHECK(circle->position.y > 10.0f);
  CHECK(circle->position.y < 150.0f);       // fell, but did not reach the floor (top at y=190)
  CHECK(circle->position.x == 100.0f);      // gravity has no x component
  CHECK(floor->position.y == 200.0f);       // static body stays put

  DestroyPhysicsBody(circle);
  CHECK(GetPhysicsBodiesCount() == 1);
  DestroyPhysicsBody(floor);
  CHECK(GetPhysicsBodiesCount() == 0);

  ClosePhysics();
  puts("physac ok");
  return 0;
}
