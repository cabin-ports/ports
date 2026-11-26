#include <spy/spy.hpp>

#include <iostream>
#include <sstream>

static int failures = 0;

#define CHECK(cond)                                                                      \
  do {                                                                                   \
    if (!(cond)) {                                                                       \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #cond "\n";         \
      ++failures;                                                                        \
    }                                                                                    \
  } while (0)

template<typename T> static std::string str(T const& v)
{
  std::ostringstream os;
  os << v;
  return os.str();
}

// spy.hpp:39-47 rejects anything below C++20; a newer standard is accepted.
static_assert(spy::cpp_standard >= 20);

int main()
{
  // Mirrors test/unit/compiler.cpp:50 (msvc), :103 (clang) and :114 (gcc).
#if defined(_MSC_VER) && !defined(__clang__)
  CHECK(spy::compiler == spy::msvc_);
  CHECK(!(spy::compiler == spy::gcc_));
#elif defined(__MINGW32__)
#elif defined(__clang__)
  CHECK(spy::compiler == spy::clang_);
  CHECK(!(spy::compiler == spy::gcc_));
#elif defined(__GNUC__)
  CHECK(spy::compiler == spy::gcc_);
  CHECK(!(spy::compiler == spy::clang_));
#endif

  // Mirrors test/unit/os.cpp:59 (linux), :69 (macos) and :90 (windows).
#if defined(__linux__)
  CHECK(spy::operating_system == spy::linux_);
  CHECK(!(spy::operating_system == spy::windows_));
#elif defined(__APPLE__)
  CHECK(spy::operating_system == spy::macos_);
  CHECK(!(spy::operating_system == spy::linux_));
#elif defined(_WIN32)
  CHECK(spy::operating_system == spy::windows_);
  CHECK(!(spy::operating_system == spy::linux_));
#endif

  // Mirrors test/unit/arch.cpp:29 (amd64) and :48 (arm).
#if defined(__x86_64__) || defined(_M_X64)
  CHECK(spy::architecture == spy::amd64_);
  CHECK(!(spy::architecture == spy::arm_));
#elif defined(__aarch64__)
  CHECK(spy::architecture == spy::arm_);
  CHECK(!(spy::architecture == spy::amd64_));
#endif

  // Mirrors test/unit/data_model.cpp:17-23; llp64 is deliberately not asserted.
#if defined(__LP64__)
  CHECK(spy::data_model == spy::lp64_);
  CHECK(!(spy::data_model == spy::ilp64_));
  CHECK(!(spy::data_model == spy::silp64_));
#endif

  // The streaming used by upstream's test_package/main.cpp.
  CHECK(!str(spy::operating_system).empty());
  CHECK(!str(spy::architecture).empty());
  CHECK(!str(spy::simd_instruction_set).empty());
  CHECK(!str(spy::compiler).empty());
  CHECK(!str(spy::libc).empty());
  CHECK(!str(spy::stdlib).empty());
  std::cout << spy::operating_system << ' ' << spy::architecture << ' ' << spy::compiler << '\n';

  return failures == 0 ? 0 : 1;
}
