#include "clockUtils/argParser/ArgumentParser.h"
#include "clockUtils/errors.h"

#include <cstdio>
#include <string>

int main() {
  REGISTER_VARIABLE(std::string, mode, m, "fast", "A mode");
  REGISTER_VARIABLE_ARGUMENTS(rest);
  const char * args[] = {"--mode", "slow", "a.txt", "b.txt"};
  if (PARSE_ARGUMENTS(args, 4) != clockUtils::ClockError::SUCCESS || mode != std::string("slow") ||
      rest.size() != 2 || rest[0] != "a.txt" || rest[1] != "b.txt") {
    std::puts("FAIL argparser");
    return 1;
  }
  return 0;
}
