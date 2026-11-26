// The same 32x32 image as main.c (an 8x8 bright square at 12..19 and an
// isolated bright pixel at (26,26), threshold 50), stored with a row stride
// of 36 whose padding is filled with 255, which detection must never read
// as image.  fast.h has no extern "C" guard, so the include is wrapped.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include <fast.h>
}

namespace {

const int W = 32;
const int STRIDE = 36;
const int THRESHOLD = 50;

int failures = 0;

void check(bool ok, int line, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), __LINE__, #cond)

typedef xy *(*detect_fn)(const byte *, int, int, int, int, int *);

xy make_xy(int x, int y) {
  xy p;
  p.x = x;
  p.y = y;
  return p;
}

// Runs one detector and checks its exact corner list.
void expect(detect_fn fn, const byte *im, const std::vector<xy> &want) {
  int n = -1;
  xy *got = fn(im, W, W, STRIDE, THRESHOLD, &n);
  bool same = n == static_cast<int>(want.size());
  for (int i = 0; same && i < n; ++i)
    same = got[i].x == want[i].x && got[i].y == want[i].y;
  CHECK(same);
  std::free(got);
}

}  // namespace

int main() {
  std::vector<byte> im(STRIDE * W, 255);
  std::vector<byte> flat(STRIDE * W, 128);
  for (int y = 0; y < W; ++y) {
    std::memset(&im[y * STRIDE], 0, W);
    std::memset(&flat[y * STRIDE], 128, W);
    // Padding of the uniform image differs from its pixels.
    std::memset(&flat[y * STRIDE + W], 255, STRIDE - W);
  }
  for (int y = 12; y <= 19; ++y)
    for (int x = 12; x <= 19; ++x) im[y * STRIDE + x] = 255;
  im[26 * STRIDE + 26] = 255;

  std::vector<xy> dot(1, make_xy(26, 26));
  std::vector<xy> all;
  all.push_back(make_xy(12, 12));
  all.push_back(make_xy(19, 12));
  all.push_back(make_xy(12, 19));
  all.push_back(make_xy(19, 19));
  all.push_back(make_xy(26, 26));
  std::vector<xy> none;

  // A square corner has 11 contiguous background ring pixels, so fast12
  // misses it; fast9 and fast10 also fire next to the corners, where
  // equal scores make nonmax suppression drop them all.
  expect(fast9_detect_nonmax, &im[0], dot);
  expect(fast10_detect_nonmax, &im[0], dot);
  expect(fast11_detect_nonmax, &im[0], all);
  expect(fast12_detect_nonmax, &im[0], dot);
  expect(fast11_detect, &im[0], all);
  expect(fast12_detect, &im[0], dot);

  int n = -1;
  std::free(fast9_detect(&im[0], W, W, STRIDE, THRESHOLD, &n));
  CHECK(n == 25);
  std::free(fast10_detect(&im[0], W, W, STRIDE, THRESHOLD, &n));
  CHECK(n == 13);

  expect(fast9_detect_nonmax, &flat[0], none);
  expect(fast11_detect_nonmax, &flat[0], none);

  // The separate stages agree with the combined call.
  xy *corners = fast11_detect(&im[0], W, W, STRIDE, THRESHOLD, &n);
  int *scores = fast11_score(&im[0], STRIDE, corners, n, THRESHOLD);
  int m = -1;
  xy *nonmax = nonmax_suppression(corners, scores, n, &m);
  CHECK(n == 5 && m == 5 && scores[4] == 254);
  std::free(nonmax);
  std::free(scores);
  std::free(corners);

  if (failures) return EXIT_FAILURE;
  std::puts("fastfeat C++ ok");
  return EXIT_SUCCESS;
}
