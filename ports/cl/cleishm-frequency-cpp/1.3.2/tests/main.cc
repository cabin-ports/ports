#include <frequency/frequency.hpp>

#include <chrono>
#include <cstdio>

using namespace freq;
using namespace frequency_literals;

static int failures = 0;
#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);  \
      ++failures;                                         \
    }                                                     \
  } while (0)

int main() {
  CHECK(kilohertz{1} == hertz{1000});
  CHECK((1000_Hz + 500_Hz).count() == 1500);
  CHECK((5_GHz - 2400_MHz) == 2600_MHz);
  CHECK(1000_Hz / 100_Hz == 10);
  CHECK((100_Hz * 3).count() == 300);
  CHECK(hertz(millihertz{44100000}).count() == 44100);
  CHECK(millihertz(hertz{2}).count() == 2000);
  CHECK(hertz{1000}.period<std::chrono::milliseconds>() ==
        std::chrono::milliseconds{1});
  CHECK(kilohertz{1} < megahertz{1});

  CHECK(to_string(kilohertz(433)) == "433kHz");
  CHECK(to_string(millihertz(1500)) == "1500mHz");
  CHECK(to_string(millihertz(1500), base_unit) == "1.5Hz");
  CHECK(to_string(megahertz(2400), scaled) == "2.4GHz");
#if CONFIG_FREQUENCY_STD_FORMAT
  CHECK(std::format("{}", millihertz{1500}) == "1500mHz");
  CHECK(std::format("{:.1f}", millihertz{1500}) == "1500.0mHz");
  CHECK(std::format("{:b.1f}", millihertz{1500}) == "1.5Hz");
  CHECK(std::format("{:s}", 2400_MHz) == "2.4GHz");
#endif
  return failures;
}
