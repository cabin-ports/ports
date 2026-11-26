#include <tweeners/builder.hpp>
#include <tweeners/easing.hpp>
#include <tweeners/system.hpp>

#include <cmath>
#include <cstdio>

static int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  // Linear tween on an int, from tests/src/start_update.cpp:7-33 (assign_address).
  {
    int value = -1;
    tweeners::system system;
    tweeners::builder()
        .range_transform(0, 100, 10, value, &tweeners::easing::linear<float>)
        .build(system);

    system.update(1);
    CHECK(value == 10);
    system.update(1);
    CHECK(value == 20);
    system.update(1);
    CHECK(value == 30);
    system.update(2);
    CHECK(value == 50);
    system.update(3);
    CHECK(value == 80);
    system.update(3);
    CHECK(value == 100);
  }

  // quad is t * t (include/tweeners/detail/easing.tpp:59-63); ease_out is
  // 1 - f(1 - t) (:17-24).  The points are exact in binary floating point.
  {
    CHECK(tweeners::easing::quad<float>(0.5f) == 0.25f);
    CHECK(tweeners::easing::ease_out(0.5f, &tweeners::easing::quad<float>) == 0.75f);

    // sine is 1 - cos(t * pi / 2) (:55).
    CHECK(tweeners::easing::sine<float>(0.0f) == 0.0f);
    CHECK(std::fabs(tweeners::easing::sine<float>(1.0f) - 1.0f) < 1e-6f);
    CHECK(std::fabs(tweeners::easing::sine<float>(0.5f) - 0.2928932f) < 1e-6f);

    int value = -1;
    tweeners::system system;
    tweeners::builder()
        .range_transform(0, 100, 8, value, &tweeners::easing::quad<float>)
        .build(system);

    system.update(2);
    CHECK(value == 6);
    system.update(2);
    CHECK(value == 25);
    system.update(2);
    CHECK(value == 56);
    system.update(2);
    CHECK(value == 100);
  }

  // on_done fires once, after the last update, as in tests/src/on_start_on_done.cpp.
  {
    int value = -1;
    int done = 0;
    int updates = 0;
    tweeners::system system;
    tweeners::builder()
        .range_transform(0, 100, 4, [&](int v) { value = v; ++updates; },
                         &tweeners::easing::linear<float>)
        .on_done([&]() { ++done; })
        .build(system);

    system.update(2);
    CHECK(done == 0);
    CHECK(value == 50);
    system.update(2);
    CHECK(done == 1);
    CHECK(value == 100);
    system.update(2);
    CHECK(done == 1);
    CHECK(updates == 2);
  }

  return failures == 0 ? 0 : 1;
}
