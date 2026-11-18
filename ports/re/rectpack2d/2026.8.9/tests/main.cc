// rectpack2D's own example (example/main.cpp, all four ways to pack) checked against the output
// upstream's CI diffs it with (tests/example_expected_output.txt), then packing invariants, the
// stack allocator, packing without flips, and the insertion callbacks.
#include <rectpack2D/finders_interface.h>

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                                          \
  do {                                                                                       \
    if (!(cond)) {                                                                           \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);          \
      ++failures;                                                                            \
    }                                                                                        \
  } while (0)

using namespace rectpack2D;

using spaces_type = empty_spaces<true, default_empty_spaces>;
using rect_type = output_rect_t<spaces_type>;

const rect_wh inputs[5] = {{20, 40}, {120, 40}, {85, 59}, {199, 380}, {85, 875}};

// A bin followed by each rectangle's x, y, w and h.
struct Expected {
  rect_wh bin;
  int rects[5][4];
};

// tests/example_expected_output.txt, one block per example.
const Expected examples[4] = {
    {{284, 875}, {{170, 380, 20, 40}, {85, 439, 120, 40}, {85, 380, 85, 59}, {85, 0, 199, 380}, {0, 0, 85, 875}}},
    {{509, 875}, {{489, 0, 20, 40}, {369, 0, 120, 40}, {284, 0, 85, 59}, {0, 0, 199, 380}, {199, 0, 85, 875}}},
    {{284, 974}, {{0, 0, 20, 40}, {20, 0, 120, 40}, {0, 40, 85, 59}, {0, 99, 199, 380}, {199, 99, 85, 875}}},
    {{304, 974}, {{0, 0, 20, 40}, {20, 0, 120, 40}, {20, 40, 85, 59}, {20, 99, 199, 380}, {219, 99, 85, 875}}},
};

template <class Rect> bool overlap(const Rect &a, const Rect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// Each rectangle keeps its input size, possibly turned by 90 degrees, lies inside the bin, and
// overlaps no other; the bin is their bounding box.
template <class Rect>
void checkPacking(const std::vector<Rect> &rects, const rect_wh &bin, bool mayTurn) {
  rect_wh box;
  for (std::size_t i = 0; i < rects.size(); ++i) {
    const Rect &r = rects[i];
    const bool same = r.w == inputs[i].w && r.h == inputs[i].h;
    const bool turned = r.w == inputs[i].h && r.h == inputs[i].w;
    CHECK(same || (mayTurn && turned));
    CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= bin.w && r.y + r.h <= bin.h);
    for (std::size_t j = 0; j < i; ++j)
      CHECK(!overlap(r, rects[j]));
    box.expand_with(r);
  }
  CHECK(box.w == bin.w && box.h == bin.h);
}

bool matches(const std::vector<rect_type> &rects, const rect_wh &bin, const Expected &want) {
  if (bin.w != want.bin.w || bin.h != want.bin.h)
    return false;
  for (std::size_t i = 0; i < 5; ++i) {
    const rect_type &r = rects[i];
    if (r.x != want.rects[i][0] || r.y != want.rects[i][1] || r.w != want.rects[i][2] ||
        r.h != want.rects[i][3])
      return false;
  }
  return true;
}

void checkExample(const std::vector<rect_type> &rects, const rect_wh &bin, const Expected &want,
                  const Expected *tied = nullptr) {
  CHECK(matches(rects, bin, want) || (tied && matches(rects, bin, *tied)));
  checkPacking(rects, bin, true);
}

callback_result keepGoing(rect_type &) { return callback_result::CONTINUE_PACKING; }
callback_result giveUp(rect_type &) { return callback_result::ABORT_PACKING; }

// Upstream's example: one vector goes through all four examples, so each starts from the previous
// one's (possibly turned) sizes.
template <class Allocator> void runExamples() {
  using spaces = empty_spaces<true, Allocator>;
  const int max_side = 1000;
  const int discard_step = -4;

  std::vector<rect_type> rects;
  for (const rect_wh &in : inputs)
    rects.emplace_back(rect_xywh(0, 0, in.w, in.h));

  // The winning height order ties 20x40 with 120x40 and std::sort is not stable; the other tie
  // order puts 20x40 at (205,439) and leaves everything else as it is.
  Expected tied = examples[0];
  tied.rects[0][0] = 205;
  tied.rects[0][1] = 439;
  rect_wh bin = find_best_packing<spaces>(
      rects, make_finder_input(max_side, discard_step, keepGoing, giveUp, flipping_option::ENABLED));
  checkExample(rects, bin, examples[0], &tied);

  using rect_ptr = rect_type *;
  bin = find_best_packing<spaces>(
      rects, make_finder_input(max_side, discard_step, keepGoing, giveUp, flipping_option::ENABLED),
      [](const rect_ptr a, const rect_ptr b) { return a->get_wh().area() > b->get_wh().area(); },
      [](const rect_ptr a, const rect_ptr b) { return a->get_wh().perimeter() < b->get_wh().perimeter(); });
  checkExample(rects, bin, examples[1]);

  bin = find_best_packing_dont_sort<spaces>(
      rects, make_finder_input(max_side, discard_step, keepGoing, giveUp, flipping_option::ENABLED));
  checkExample(rects, bin, examples[2]);

  spaces root({max_side, max_side});
  root.flipping_mode = flipping_option::ENABLED;
  for (rect_type &r : rects) {
    const auto inserted = root.insert(std::as_const(r).get_wh());
    CHECK(inserted.has_value());
    if (!inserted)
      break;
    r = *inserted;
  }
  checkExample(rects, root.get_rects_aabb(), examples[3]);
}

// The example never turns a rectangle; these single insertions do.
void checkFlips() {
  // Only the turned rectangle fits.
  spaces_type tall({100, 50});
  const auto turned = tall.insert(rect_wh(50, 100));
  CHECK(turned && turned->flipped && turned->x == 0 && turned->y == 0 && turned->w == 100 && turned->h == 50);

  // Both fit; turned, it fills the height and leaves one empty space instead of two.
  spaces_type wide({100, 50});
  const auto better = wide.insert(rect_wh(50, 40));
  CHECK(better && better->flipped && better->w == 40 && better->h == 50);
  const auto rest = wide.insert(rect_wh(60, 50));
  CHECK(rest && !rest->flipped && rest->x == 40 && rest->y == 0);

  // With flipping disabled at run time nothing turns.
  spaces_type fixed({100, 50});
  fixed.flipping_mode = flipping_option::DISABLED;
  CHECK(!fixed.insert(rect_wh(50, 100)));
  const auto upright = fixed.insert(rect_wh(50, 40));
  CHECK(upright && !upright->flipped && upright->w == 50 && upright->h == 40);

  // A full-width rectangle leaves one space below it, which the next one fills exactly.
  empty_spaces<false> strip({100, 50});
  const auto top = strip.insert(rect_wh(100, 20));
  const auto bottom = strip.insert(rect_wh(100, 30));
  CHECK(top && top->y == 0 && bottom && bottom->x == 0 && bottom->y == 20);
  CHECK(!strip.insert(rect_wh(1, 1)));

  // A stack allocator with one slot takes a single split but not two.
  empty_spaces<false, static_empty_spaces<1>> one({100, 50});
  CHECK(one.insert(rect_wh(100, 20)));
  CHECK(!one.insert(rect_wh(50, 10)));
}

}  // namespace

int main() {
  runExamples<default_empty_spaces>();
  // The stack allocator stores the same empty spaces, so it packs as the vector one does.
  runExamples<static_empty_spaces<64>>();
  checkFlips();

  const int max_side = 1000;
  rect_wh bin;

  // Without flipping the output type has no flag and no rectangle turns.
  using fixed_spaces = empty_spaces<false>;
  std::vector<rect_xywh> upright;
  for (const rect_wh &in : inputs)
    upright.emplace_back(0, 0, in.w, in.h);
  int placed = 0;
  bin = find_best_packing<fixed_spaces>(
      upright,
      make_finder_input(
          max_side, 1, [&placed](rect_xywh &) { ++placed; return callback_result::CONTINUE_PACKING; },
          [](rect_xywh &) { return callback_result::ABORT_PACKING; }, flipping_option::DISABLED));
  CHECK(placed == 5);
  checkPacking(upright, bin, false);

  // A rectangle wider than the largest bin is reported unsuccessful; aborting stops the packing.
  std::vector<rect_xywh> tooWide{rect_xywh(0, 0, 50, 50), rect_xywh(0, 0, 150, 10), rect_xywh(0, 0, 30, 30)};
  int ok = 0, failed = 0, failedWidth = 0;
  bin = find_best_packing_dont_sort<fixed_spaces>(
      tooWide,
      make_finder_input(
          100, 1, [&ok](rect_xywh &) { ++ok; return callback_result::CONTINUE_PACKING; },
          [&](rect_xywh &r) {
            ++failed;
            failedWidth = r.w;
            return callback_result::ABORT_PACKING;
          },
          flipping_option::DISABLED));
  CHECK(ok == 1 && failed == 1 && failedWidth == 150);
  CHECK(bin.w == 50 && bin.h == 50);
  CHECK(tooWide[0].x == 0 && tooWide[0].y == 0);

  // Continuing past the failure places the last rectangle too; the failed one is left as it was.
  std::vector<rect_xywh> skipped{rect_xywh(0, 0, 50, 50), rect_xywh(7, 7, 150, 10), rect_xywh(7, 7, 30, 30)};
  ok = failed = 0;
  bin = find_best_packing_dont_sort<fixed_spaces>(
      skipped,
      make_finder_input(
          100, 1, [&ok](rect_xywh &) { ++ok; return callback_result::CONTINUE_PACKING; },
          [&failed](rect_xywh &) { ++failed; return callback_result::CONTINUE_PACKING; },
          flipping_option::DISABLED));
  const rect_xywh &last = skipped[2];
  CHECK(ok == 2 && failed == 1);
  CHECK(skipped[1].x == 7 && skipped[1].y == 7);
  CHECK(last.w == 30 && last.h == 30 && last.x + 30 <= 100 && last.y + 30 <= 100 && !overlap(last, skipped[0]));
  CHECK(bin.w == std::max(50, last.x + 30) && bin.h == std::max(50, last.y + 30));

  // Aborting after a successful insertion leaves the remaining rectangles untouched.
  std::vector<rect_xywh> first{rect_xywh(0, 0, 50, 50), rect_xywh(7, 7, 30, 30)};
  ok = 0;
  bin = find_best_packing_dont_sort<fixed_spaces>(
      first,
      make_finder_input(
          100, 1, [&ok](rect_xywh &) { ++ok; return callback_result::ABORT_PACKING; },
          [](rect_xywh &) { return callback_result::ABORT_PACKING; }, flipping_option::DISABLED));
  CHECK(ok == 1 && bin.w == 50 && bin.h == 50);
  CHECK(first[1].x == 7 && first[1].y == 7);

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("rectpack2d: ok\n");
  return 0;
}
