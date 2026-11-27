/* The C consumer: version, moment and area helpers, and a ball dropped
 * under gravity in a space stepped a fixed number of times. */
#include <chipmunk/chipmunk.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
      failures++;                                                        \
    }                                                                    \
  } while (0)

static int near(cpFloat a, cpFloat b) { return fabs(a - b) < 1e-9; }

int main(void) {
  cpVect square[4];
  cpSpace *space;
  cpBody *ball;
  cpShape *shape, *floor_;
  int i;

  CHECK(strcmp(cpVersionString, "7.0.3") == 0);
  CHECK(CP_VERSION_MAJOR == 7 && CP_VERSION_MINOR == 0 && CP_VERSION_RELEASE == 3);

  /* Moments: solid disc m*r^2/2, hollow ring m*(r1^2+r2^2)/2. */
  CHECK(near(cpMomentForCircle(2, 0, 3, cpvzero), 9.0));
  CHECK(near(cpMomentForCircle(2, 1, 3, cpvzero), 10.0));
  CHECK(near(cpMomentForBox(12, 2, 3), 12.0 * (4 + 9) / 12.0));

  square[0] = cpv(0, 0);
  square[1] = cpv(2, 0);
  square[2] = cpv(2, 2);
  square[3] = cpv(0, 2);
  CHECK(near(cpAreaForPoly(4, square, 0), 4.0));
  CHECK(near(cpAreaForPoly(4, square, 1), 4.0 + 8.0 + 3.14159265358979323846));
  CHECK(near(cpAreaForCircle(0, 2), 4.0 * 3.14159265358979323846));
  CHECK(near(cpvlength(cpv(3, 4)), 5.0));
  CHECK(near(cpvdot(cpv(1, 2), cpv(3, 4)), 11.0));

  /* A ball falls from y=10 under gravity -10 for 0.5 s (30 steps of 1/60 s)
   * and rests on a static segment at y=0.  Position integrates before velocity: vy after
   * n steps is -10*n*dt, and y drops by 10*dt^2*n(n-1)/2. */
  space = cpSpaceNew();
  cpSpaceSetGravity(space, cpv(0, -10));
  ball = cpSpaceAddBody(space, cpBodyNew(1, cpMomentForCircle(1, 0, 1, cpvzero)));
  cpBodySetPosition(ball, cpv(0, 10));
  shape = cpSpaceAddShape(space, cpCircleShapeNew(ball, 1, cpvzero));
  cpShapeSetFriction(shape, 0.5);
  floor_ = cpSpaceAddShape(space, cpSegmentShapeNew(cpSpaceGetStaticBody(space),
                                                    cpv(-20, 0), cpv(20, 0), 0));
  CHECK(cpSpaceContainsBody(space, ball));
  CHECK(cpSpaceContainsShape(space, floor_));

  for (i = 0; i < 30; i++) cpSpaceStep(space, 1.0 / 60.0);
  CHECK(near(cpBodyGetVelocity(ball).y, -10.0 * 30 / 60.0));
  CHECK(near(cpBodyGetPosition(ball).y, 10.0 - 10.0 * (30 * 29 / 2) / 3600.0));
  CHECK(near(cpBodyGetPosition(ball).x, 0.0));

  /* Falls 10 - 1 = 9 units to touch the floor (about 1.34 s); after 4 s
   * the ball has landed and rests on it. */
  for (i = 0; i < 210; i++) cpSpaceStep(space, 1.0 / 60.0);
  CHECK(fabs(cpBodyGetPosition(ball).y - 1.0) < 0.05);
  CHECK(fabs(cpBodyGetVelocity(ball).y) < 0.5);

  /* Point query hits the ball's shape. */
  CHECK(cpSpacePointQueryNearest(space, cpBodyGetPosition(ball), 0,
                                 CP_SHAPE_FILTER_ALL, NULL) == shape);

  cpSpaceRemoveShape(space, shape);
  cpShapeFree(shape);
  cpSpaceRemoveBody(space, ball);
  cpBodyFree(ball);
  cpSpaceRemoveShape(space, floor_);
  cpShapeFree(floor_);
  cpSpaceFree(space);

  if (failures) return 1;
  puts("ok");
  return 0;
}
