// Upstream builds its tests with -ffp-contract=off too: the determinism
// scene must not fuse its multiply-adds.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#include <box2d/box2d.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#define CHECK(cond)                                                         \
  do {                                                                      \
    if (!(cond)) {                                                          \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                   #cond);                                                  \
      return 1;                                                             \
    }                                                                       \
  } while (0)

// Upstream's HelloWorld (test/test_world.c, whose notice follows): a box
// dropped onto the ground comes to rest on it.
// SPDX-FileCopyrightText: 2023 Erin Catto
// SPDX-License-Identifier: MIT
static int hello_world() {
  b2WorldDef worldDef = b2DefaultWorldDef();
  worldDef.gravity = b2Vec2{0.0f, -10.0f};
  b2WorldId worldId = b2CreateWorld(&worldDef);
  CHECK(b2World_IsValid(worldId));

  b2BodyDef groundBodyDef = b2DefaultBodyDef();
  groundBodyDef.position = b2Pos{0.0f, -10.0f};
  b2BodyId groundId = b2CreateBody(worldId, &groundBodyDef);
  b2Polygon groundBox = b2MakeBox(50.0f, 10.0f);
  b2ShapeDef groundShapeDef = b2DefaultShapeDef();
  b2CreatePolygonShape(groundId, &groundShapeDef, &groundBox);

  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_dynamicBody;
  bodyDef.position = b2Pos{0.0f, 4.0f};
  b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
  b2Polygon dynamicBox = b2MakeBox(1.0f, 1.0f);
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.density = 1.0f;
  shapeDef.material.friction = 0.3f;
  b2CreatePolygonShape(bodyId, &shapeDef, &dynamicBox);

  for (int i = 0; i < 90; ++i) {
    b2World_Step(worldId, 1.0f / 60.0f, 4);
  }

  b2Pos position = b2Body_GetPosition(bodyId);
  b2Rot rotation = b2Body_GetRotation(bodyId);
  CHECK(b2AbsFloat(position.x) < 0.01f);
  CHECK(b2AbsFloat(position.y - 1.0f) < 0.01f);
  CHECK(b2AbsFloat(b2Rot_GetAngle(rotation)) < 0.01f);

  b2DestroyWorld(worldId);
  CHECK(!b2World_IsValid(worldId));
  return 0;
}

// The scene of shared/determinism.c, whose notice follows, as
// test/test_determinism.c runs it: upstream expects the same sleep step and
// transform hash on every platform, worker count and SIMD path
// (shared/determinism.h, single precision).
// SPDX-FileCopyrightText: 2022 Erin Catto
// SPDX-License-Identifier: MIT
static int falling_hinges(int workerCount, bool sse2Fallback) {
  b2WorldDef worldDef = b2DefaultWorldDef();
  worldDef.workerCount = workerCount;
  b2WorldId worldId = b2CreateWorld(&worldDef);
  b2World_EnableSSE2Fallback(worldId, sse2Fallback);

  {
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.position = b2Pos{0.0f, -1.0f};
    b2BodyId groundId = b2CreateBody(worldId, &bodyDef);
    b2Polygon box = b2MakeBox(40.0f, 1.0f);
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    b2CreatePolygonShape(groundId, &shapeDef, &box);
  }

  const int columnCount = 4;
  const int rowCount = 20;
  std::vector<b2BodyId> bodyIds;

  const float h = 0.25f;
  b2Polygon box = b2MakeSquare(h);
  b2ShapeDef shapeDef = b2DefaultShapeDef();

  b2RevoluteJointDef jointDef = b2DefaultRevoluteJointDef();
  jointDef.enableLimit = true;
  jointDef.lowerAngle = -0.1f * B2_PI;
  jointDef.upperAngle = 0.2f * B2_PI;
  jointDef.enableSpring = true;
  jointDef.hertz = 1.0f;
  jointDef.dampingRatio = 1.0f;
  jointDef.enableMotor = true;
  jointDef.maxMotorTorque = 0.25f;
  jointDef.base.localFrameA.p = b2Vec2{-h, h};
  jointDef.base.localFrameB.p = b2Vec2{-h, -h};
  jointDef.base.constraintHertz = 60.0f;
  jointDef.base.constraintDampingRatio = 0.0f;
  jointDef.base.drawScale = 0.5f;

  const float offset = 0.4f * h;
  const float dx = 10.0f * h;
  const float xBase = -0.5f * dx * (columnCount - 1.0f);

  for (int j = 0; j < columnCount; ++j) {
    float x = xBase + j * dx;
    b2BodyId prevBodyId = b2_nullBodyId;

    for (int i = 0; i < rowCount; ++i) {
      b2BodyDef bodyDef = b2DefaultBodyDef();
      bodyDef.type = b2_dynamicBody;
      bodyDef.position.x = x + offset * i;
      bodyDef.position.y = h + 2.0f * h * i;
      bodyDef.rotation = b2MakeRot((i & 1) == 0 ? -0.1f : 0.1f);
      b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);

      if ((i & 1) == 0) {
        prevBodyId = bodyId;
      } else {
        jointDef.base.bodyIdA = prevBodyId;
        jointDef.base.bodyIdB = bodyId;
        b2CreateRevoluteJoint(worldId, &jointDef);
        prevBodyId = b2_nullBodyId;
      }

      b2CreatePolygonShape(bodyId, &shapeDef, &box);
      bodyIds.push_back(bodyId);
    }
  }
  CHECK(static_cast<int>(bodyIds.size()) == columnCount * rowCount);

  int stepCount = 0;
  int sleepStep = -1;
  std::uint32_t hash = 0;
  while (hash == 0) {
    CHECK(stepCount < 1000);
    b2World_Step(worldId, 1.0f / 60.0f, 4);

    if (b2World_GetBodyEvents(worldId).moveCount == 0) {
      CHECK(b2World_GetAwakeBodyCount(worldId) == 0);
      hash = B2_HASH_INIT;
      for (b2BodyId id : bodyIds) {
        b2WorldTransform transform = b2Body_GetTransform(id);
        hash = b2Hash(hash, reinterpret_cast<std::uint8_t*>(&transform),
                      sizeof(b2WorldTransform));
      }
      sleepStep = stepCount;
    }
    stepCount += 1;
  }

  b2DestroyWorld(worldId);

  if (sleepStep != 299 || hash != 0x465F3DF4u) {
    std::fprintf(stderr, "workers=%d sse2=%d sleepStep=%d hash=0x%08X\n",
                 workerCount, sse2Fallback ? 1 : 0, sleepStep,
                 static_cast<unsigned>(hash));
  }
  CHECK(sleepStep == 299);
  CHECK(hash == 0x465F3DF4u);
  return 0;
}

int main() {
  b2Version version = b2GetVersion();
  CHECK(version.major == 3 && version.minor == 2 && version.revision == 0);

  if (hello_world() != 0 || falling_hinges(0, false) != 0 ||
      falling_hinges(4, false) != 0 || falling_hinges(0, true) != 0) {
    return 1;
  }
  std::puts("box2d C++ consumer: ok");
  return 0;
}
