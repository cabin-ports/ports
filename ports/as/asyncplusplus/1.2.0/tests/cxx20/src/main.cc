// A C++20 consumer: async::spawn's return type uses std::invoke_result there.
#include <async++.h>

#include <cstdio>

int main() {
  auto t = async::spawn([] { return 20; }).then([](int v) { return v + 22; });
  if (t.get() != 42) {
    std::fprintf(stderr, "spawn(...).then(...) gave a wrong value\n");
    return 1;
  }
  return 0;
}
