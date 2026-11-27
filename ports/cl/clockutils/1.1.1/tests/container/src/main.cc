#include "clockUtils/container/LockFreeQueue.h"
#include "clockUtils/errors.h"

#include <cstdio>

int main() {
  clockUtils::container::LockFreeQueue<int, 1> ring;
  int value = 0;
  if (ring.push(3) != clockUtils::ClockError::SUCCESS || ring.push(4) != clockUtils::ClockError::NO_SPACE_AVAILABLE ||
      ring.poll(value) != clockUtils::ClockError::SUCCESS || value != 3 || !ring.empty()) {
    std::puts("FAIL container");
    return 1;
  }
  return 0;
}
