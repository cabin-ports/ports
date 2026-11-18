#include <tweeny/tweeny.h>

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

static void test_linear() {
  auto t = tweeny::from(0).to(100).via(tweeny::easing::linear).during(100U).build();
  int steps = 0;
  t.on(tweeny::event::step, [&steps](auto&) {
    ++steps;
    return tweeny::event::response::ok;
  });

  CHECK(t.step(10) == 10);
  CHECK(t.peek() == 10);
  CHECK(t.step(20) == 30);
  CHECK(t.progress() == 0.3f);
  CHECK(t.step(-20) == 10);
  CHECK(steps == 3);

  // seek() moves, peek(frame) only looks.
  CHECK(t.seek(75U) == 75);
  CHECK(t.peek(40U) == 40);
  CHECK(t.peek() == 75);
  CHECK(t.seek(0U) == 0);
  CHECK(t.step(-5) == 0);
  CHECK(t.step(250) == 100);
  CHECK(t.progress() == 1.0f);

  // Integers round to nearest: 10/3 and 20/3.
  auto r = tweeny::from(0).to(10).during(3U).build();
  CHECK(r.step(1) == 3);
  CHECK(r.step(1) == 7);
  CHECK(r.step(1) == 10);
}

static void test_multi_value() {
  auto t = tweeny::from(0, 0.0f)
               .to(100, 1.0f)
               .via(tweeny::easing::linear, tweeny::easing::quadraticIn)
               .during(100U)
               .build();
  auto [i, f] = t.step(50);
  CHECK(i == 50);
  CHECK(f == 0.25f);

  auto hello = tweeny::from('h', 'e', 'l', 'l', 'o').to('w', 'o', 'r', 'l', 'd').during(50U).build();
  // Halfway: 111.5 and 105.5 round away from zero.
  auto [h1, h2, h3, h4, h5] = hello.peek(25U);
  CHECK(h1 == 'p' && h2 == 'j' && h3 == 'o' && h4 == 'l' && h5 == 'j');
  auto [w, o, r, l, d] = hello.step(50);
  CHECK(w == 'w' && o == 'o' && r == 'r' && l == 'l' && d == 'd');
}

static void test_easing() {
  auto q = tweeny::from(0.0f).to(100.0f).via(tweeny::easing::quadraticInOut).during(60U).build();
  CHECK(q.seek(15U) == 12.5f);
  CHECK(q.seek(30U) == 50.0f);
  CHECK(q.seek(45U) == 87.5f);
  CHECK(q.step(-30) == 12.5f);
}

static void test_keyframes() {
  auto t = tweeny::from(0).to(50).during(30U).to(100).during(30U).build();
  int entered = -1, left = -1, transitions = 0;
  t.on(tweeny::event::keyframeEnter, [&](auto&, auto e) {
    entered = static_cast<int>(e.key_frame);
    ++transitions;
    return tweeny::event::response::ok;
  });
  t.on(tweeny::event::keyframeLeave, [&](auto&, auto e) {
    left = static_cast<int>(e.key_frame);
    return tweeny::event::response::ok;
  });

  CHECK(t.jump(1) == 50);
  CHECK(t.keyframe() == 1);
  CHECK(left == 0 && entered == 1);
  CHECK(t.step(15) == 75);
  CHECK(transitions == 1);
  CHECK(t.step(-30) == 25);
  CHECK(t.keyframe() == 0);
  CHECK(left == 1 && entered == 0 && transitions == 2);
}

int main() {
  test_linear();
  test_multi_value();
  test_easing();
  test_keyframes();
  std::puts("tweeny 4.2.0 ok");
  return 0;
}
