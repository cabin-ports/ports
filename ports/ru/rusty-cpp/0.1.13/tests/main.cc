#include <rusty/collections/min_heap.h>
#include <rusty/option.h>
#include <rusty/primitive.h>
#include <rusty/result.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

int fail(const char* what) {
  std::fprintf(stderr, "rusty-cpp: %s\n", what);
  return 1;
}

}  // namespace

int main() {
  {
    rusty::MinHeap<int> heap(std::vector<int>{5, 3, 8, 1});
    const int expected[] = {1, 3, 5, 8};
    for (int want : expected) {
      rusty::Option<int> got = heap.pop();
      if (!got.is_some() || std::move(got).unwrap() != want) {
        return fail("heap pop order");
      }
    }
    if (!heap.is_empty() || !heap.pop().is_none()) return fail("heap not empty");
  }

  {
    rusty::Option<int> some = 7;
    rusty::Option<int> none;
    if (!some.is_some() || some.is_none()) return fail("option some");
    if (!none.is_none() || none.is_some()) return fail("option none");
    if (std::move(some).unwrap() != 7) return fail("option unwrap");
  }

  {
    rusty::Result<int, std::string> ok(42);
    rusty::Result<int, std::string> err(std::string("boom"));
    if (!ok.is_ok() || ok.is_err()) return fail("result ok");
    if (!err.is_err() || err.is_ok()) return fail("result err");
    if (std::move(ok).unwrap() != 42) return fail("result unwrap");
    if (std::move(err).unwrap_err() != "boom") return fail("result unwrap_err");
  }

  if (!rusty::is_power_of_two(8u) || rusty::is_power_of_two(6u)) {
    return fail("is_power_of_two");
  }
  if (rusty::next_power_of_two(5u) != 8u || rusty::next_power_of_two(8u) != 8u) {
    return fail("next_power_of_two");
  }
  return 0;
}
