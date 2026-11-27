// The C++ consumer: the same checks through the headers' extern "C" blocks,
// plus the cpHastySpace API, which runs its solver on worker threads.
#include <chipmunk/chipmunk.h>
extern "C" {
#include <chipmunk/cpHastySpace.h>  // declares no extern "C" itself
}

#include <cmath>
#include <cstdio>
#include <cstring>

static int failures;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static bool near(cpFloat a, cpFloat b) { return std::fabs(a - b) < 1e-9; }

// Drops a ball on a floor for 4 s and returns the final height.
static cpFloat settle(cpSpace *space, bool hasty) {
  cpSpaceSetGravity(space, cpv(0, -10));
  cpBody *ball = cpSpaceAddBody(space, cpBodyNew(1, cpMomentForCircle(1, 0, 1, cpvzero)));
  cpBodySetPosition(ball, cpv(0, 10));
  cpShape *shape = cpSpaceAddShape(space, cpCircleShapeNew(ball, 1, cpvzero));
  cpShape *floor_ = cpSpaceAddShape(
      space, cpSegmentShapeNew(cpSpaceGetStaticBody(space), cpv(-20, 0), cpv(20, 0), 0));
  for (int i = 0; i < 240; i++) {
    if (hasty)
      cpHastySpaceStep(space, 1.0 / 60.0);
    else
      cpSpaceStep(space, 1.0 / 60.0);
  }
  cpFloat y = cpBodyGetPosition(ball).y;
  cpSpaceRemoveShape(space, shape);
  cpShapeFree(shape);
  cpSpaceRemoveBody(space, ball);
  cpBodyFree(ball);
  cpSpaceRemoveShape(space, floor_);
  cpShapeFree(floor_);
  return y;
}

int main() {
  CHECK(std::strcmp(cpVersionString, "7.0.3") == 0);
  CHECK(near(cpMomentForBox(12, 2, 3), 13.0));
  CHECK(near(cpAreaForCircle(0, 2), 4.0 * 3.14159265358979323846));

  cpSpace *space = cpSpaceNew();
  CHECK(std::fabs(settle(space, false) - 1.0) < 0.05);
  cpSpaceFree(space);

  cpSpace *hasty = cpHastySpaceNew();
  cpHastySpaceSetThreads(hasty, 2);
  CHECK(cpHastySpaceGetThreads(hasty) == 2);
  CHECK(std::fabs(settle(hasty, true) - 1.0) < 0.05);
  cpHastySpaceFree(hasty);

  if (failures) return 1;
  std::puts("ok");
  return 0;
}
