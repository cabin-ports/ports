// The standalone asio scheduler runs posted work in order on Run() and stops
// when a posted task calls Stop().
#include <cli/standaloneasioscheduler.h>

#include <cstdlib>
#include <iostream>
#include <string>

int main() {
  cli::StandaloneAsioScheduler scheduler;
  std::string order;
  scheduler.Post([&] { order += 'a'; });
  scheduler.Post([&] {
    order += 'b';
    scheduler.Stop();
  });
  if (!order.empty()) {
    std::cerr << "posted work ran before Run()\n";
    return EXIT_FAILURE;
  }
  scheduler.Run();
  if (order != "ab" || !scheduler.Stopped()) {
    std::cerr << "unexpected order: " << order << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
