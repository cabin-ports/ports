#include <tqdm.hpp>

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  // Iterating a container yields its elements; the bar goes to the stream
  // set with set_ostream, keeping it out of the test's own output.
  std::vector<int> v{1, 2, 3, 4, 5, 6};
  std::ostringstream out;
  int sum = 0;
  {
    auto bar = tq::tqdm(v);
    bar.set_ostream(out);
    bar.set_prefix("P:");
    for (int a : bar) sum += a;
  }
  CHECK(sum == 21);

  // The bar ends at 100.0% (always drawn at progress 1) with our prefix.
  const std::string text = out.str();
  CHECK(text.find("\rP:{100.0%} ") != std::string::npos);

  // trange iterates [0, n).
  int tsum = 0;
  {
    auto r = tq::trange(5);
    std::ostringstream sink;
    r.set_ostream(sink);
    for (int i : r) tsum += i;
  }
  CHECK(tsum == 10);

  // range is a plain integer range with a size.
  tq::range<int> rg(1, 5);
  CHECK(rg.size() == 4);
  int rsum = 0;
  for (int i : rg) rsum += i;
  CHECK(rsum == 10);

  return failures == 0 ? 0 : 1;
}
