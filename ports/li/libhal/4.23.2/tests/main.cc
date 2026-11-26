#include <libhal/error.hpp>
#include <libhal/functional.hpp>
#include <libhal/output_pin.hpp>
#include <libhal/units.hpp>

#include <cstdio>
#include <system_error>
#include <utility>

namespace {
int failures = 0;

void check(bool ok, char const* what)
{
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

struct fake_pin : hal::output_pin
{
  settings applied{};
  bool state = false;
  int configure_calls = 0;
  int level_writes = 0;

  void driver_configure(settings const& p_settings) override
  {
    ++configure_calls;
    if (p_settings.resistor == hal::pin_resistor::pull_up &&
        !p_settings.open_drain) {
      throw hal::operation_not_supported(this);
    }
    applied = p_settings;
  }
  void driver_level(bool p_high) override
  {
    ++level_writes;
    state = p_high;
  }
  bool driver_level() override
  {
    return state;
  }
};
}  // namespace

int main()
{
  using namespace hal::literals;

  // Units and literals.
  static_assert(1_kHz == 1000U);
  static_assert(2_MHz == 2000000U);
  static_assert(3_GHz == 3000000000U);
  static_assert(1.5_kHz == 1500.0F);
  static_assert(250.0_mA == 0.25F);
  static_assert(2.5_V == 2.5F);
  static_assert(1.0_miles == 1609.344F);
  check((1_kHz) == 1000U, "1_kHz");
  check((2_MHz) != 1000U, "2_MHz differs from 1_kHz");

  // Output pin driven through the base-class API.
  fake_pin pin;
  hal::output_pin& base = pin;
  base.configure({ .resistor = hal::pin_resistor::pull_down, .open_drain = true });
  check(pin.configure_calls == 1, "configure reaches driver");
  check(pin.applied.resistor == hal::pin_resistor::pull_down &&
          pin.applied.open_drain,
        "settings forwarded");
  base.level(true);
  check(base.level(), "level high");
  base.level(false);
  check(!base.level() && pin.level_writes == 2, "level low");

  // Errors carry the errc and the instance that raised them.
  bool caught = false;
  try {
    base.configure({ .resistor = hal::pin_resistor::pull_up });
  } catch (hal::operation_not_supported const& e) {
    caught = e.error_code() == std::errc::operation_not_supported &&
             e.instance() == &pin;
  }
  check(caught, "operation_not_supported");
  check(pin.applied.resistor == hal::pin_resistor::pull_down,
        "failed configure leaves settings");
  hal::exception const base_ex = hal::timed_out(&pin);
  check(base_ex.error_code() == std::errc::timed_out, "timed_out errc");

  // Owning callback and non-owning function_ref.
  int const k = 3;
  hal::callback<int(int)> cb = [k](int x) { return x * k; };
  check(cb(7) == 21, "callback");
  hal::callback<int(int)> moved = std::move(cb);
  check(moved(2) == 6, "moved callback");
  auto lambda = [&](int x) { return x + k; };
  hal::function_ref<int(int)> ref = lambda;
  check(ref(4) == 7, "function_ref");
  hal::inplace_function<int(), 32> fixed = [] { return 42; };
  check(fixed() == 42, "inplace_function");
  static_assert(sizeof(hal::callback<void()>) >= sizeof(void*) * 2);

  if (failures == 0) {
    std::puts("ok");
  }
  return failures;
}
