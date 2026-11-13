#include <status-code/error.hpp>
#include <status-code/generic_code.hpp>
#include <status-code/posix_code.hpp>
#include <status-code/status_error.hpp>
#include <status-code/system_code.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace se = system_error2;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool same(const se::status_code_domain::string_ref& s, const char* expected) {
  return s.size() == std::strlen(expected) &&
         std::memcmp(s.data(), expected, s.size()) == 0;
}

// A hand-written custom domain for an application's own error enum.
enum class Fault : int { none = 0, timeout = 1, denied = 2 };

class FaultDomain;
using FaultCode = se::status_code<FaultDomain>;

se::errc ToErrc(Fault f) {
  switch (f) {
    case Fault::none:
      return se::errc::success;
    case Fault::timeout:
      return se::errc::timed_out;
    case Fault::denied:
      return se::errc::permission_denied;
  }
  return se::errc::unknown;
}

class FaultDomain final : public se::status_code_domain {
  using _base = se::status_code_domain;

 public:
  using value_type = Fault;

  constexpr FaultDomain() noexcept : _base(0x6a1c2b9e5f3d4c71ULL) {}
  static inline constexpr const FaultDomain& get();

  int _do_name(_vtable_name_args& args) const noexcept override {
    args.ret = string_ref("fault domain");
    return 0;
  }
  void _do_payload_info(
      _vtable_payload_info_args& args) const noexcept override {
    args.ret = {sizeof(value_type),
                sizeof(status_code_domain*) + sizeof(value_type),
                alignof(value_type) > alignof(status_code_domain*)
                    ? alignof(value_type)
                    : alignof(status_code_domain*)};
  }
  bool _do_failure(const se::status_code<void>& code) const noexcept override {
    return static_cast<const FaultCode&>(code).value() != Fault::none;
  }
  bool _do_equivalent(const se::status_code<void>& code1,
                      const se::status_code<void>& code2) const
      noexcept override {
    const FaultCode& c1 = static_cast<const FaultCode&>(code1);
    if (code2.domain() == *this) {
      return c1.value() == static_cast<const FaultCode&>(code2).value();
    }
    if (code2.domain() == se::generic_code_domain) {
      return ToErrc(c1.value()) ==
             static_cast<const se::generic_code&>(code2).value();
    }
    return false;
  }
  void _do_generic_code(
      _vtable_generic_code_args& args) const noexcept override {
    args.ret = se::generic_code(
        ToErrc(static_cast<const FaultCode&>(args.code).value()));
  }
  int _do_message(_vtable_message_args& args) const noexcept override {
    switch (static_cast<const FaultCode&>(args.code).value()) {
      case Fault::none:
        args.ret = string_ref("no fault");
        return 0;
      case Fault::timeout:
        args.ret = string_ref("timed out");
        return 0;
      case Fault::denied:
        args.ret = string_ref("access denied");
        return 0;
    }
    args.ret = string_ref("unknown fault");
    return 0;
  }
  void _do_throw_exception(const se::status_code<void>& code) const override {
    throw se::status_error<FaultDomain>(static_cast<const FaultCode&>(code));
  }
};

constexpr FaultDomain fault_domain;
inline constexpr const FaultDomain& FaultDomain::get() { return fault_domain; }

// A result-style API: success, or a failure the caller compares by meaning.
se::system_code ParsePort(const char* text, int& port) {
  long v = 0;
  if (*text == '\0') return se::generic_code(se::errc::invalid_argument);
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return se::generic_code(se::errc::invalid_argument);
    }
    v = v * 10 + (*p - '0');
    if (v > 65535) return se::generic_code(se::errc::result_out_of_range);
  }
  port = static_cast<int>(v);
  return se::generic_code(se::errc::success);
}

}  // namespace

int main() {
  // Custom domain: success and failure, messages, and meaning through errc.
  {
    const FaultCode ok(Fault::none);
    const FaultCode denied(Fault::denied);
    check(ok.success() && !ok.failure(), "custom: none is success");
    check(same(ok.message(), "no fault"), "custom: none message");
    check(ok == se::errc::success, "custom: none == errc::success");
    check(denied.failure(), "custom: denied is failure");
    check(denied.value() == Fault::denied, "custom: denied value");
    check(same(denied.message(), "access denied"), "custom: denied message");
    check(same(denied.domain().name(), "fault domain"), "custom: domain name");
    check(denied.domain().id() == 0x6a1c2b9e5f3d4c71ULL, "custom: domain id");
    check(denied == se::errc::permission_denied,
          "custom: denied == errc::permission_denied");
    check(denied != se::errc::timed_out, "custom: denied != errc::timed_out");
    check(denied == FaultCode(Fault::denied), "custom: same domain, same value");
    check(denied != FaultCode(Fault::timeout), "custom: same domain, other value");
    // Across domains, through the generic code each side maps to.
    check(denied == se::posix_code(EACCES), "custom: denied == posix EACCES");
    check(denied != se::posix_code(ENOENT), "custom: denied != posix ENOENT");
  }

  // Generic and system codes.
  {
    const se::posix_code enoent(ENOENT);
    const se::generic_code generic(se::errc::no_such_file_or_directory);
    check(enoent.failure() && enoent.value() == ENOENT, "posix: ENOENT");
    check(same(enoent.domain().name(), "posix domain"), "posix: domain name");
    check(same(generic.message(), "No such file or directory"),
          "generic: message");
    check(same(generic.domain().name(), "generic domain"),
          "generic: domain name");
    check(enoent == generic && generic == enoent, "posix == generic");
    check(enoent != se::errc::permission_denied, "posix != other errc");

    // system_code erases the domain and keeps value and meaning.
    const se::system_code sys = enoent;
    check(sys.failure(), "system: failure");
    check(sys.domain() == se::posix_code_domain, "system: posix domain");
    check(sys == se::errc::no_such_file_or_directory, "system: errc");
    check(sys == generic, "system == generic");

    const se::system_code fault = FaultCode(Fault::timeout);
    check(fault.domain() == fault_domain, "system: custom domain kept");
    check(same(fault.message(), "timed out"), "system: custom message");
    check(fault == se::errc::timed_out, "system: custom errc");
    check(fault == FaultCode(Fault::timeout), "system == custom");
    check(fault != sys, "system: custom != posix");
#ifdef _WIN32
    // ERROR_FILE_NOT_FOUND, mapped by the shipped Win32 table.
    const se::win32_code missing(0x2);
    check(missing.failure(), "win32: failure");
    check(same(missing.domain().name(), "win32 domain"), "win32: domain");
    check(missing == se::errc::no_such_file_or_directory, "win32: errc");
    check(se::system_code(missing) == sys, "win32 == posix ENOENT");
#endif
  }

  // errored_status_code, the erased error type, and the result-style API.
  {
    const se::errored_status_code<FaultDomain> timeout(Fault::timeout);
    check(timeout.value() == Fault::timeout, "errored: value");
    check(timeout == se::errc::timed_out, "errored: errc");

    const se::error err = FaultCode(Fault::denied);
    check(err.failure(), "error: failure");
    check(err.domain() == fault_domain, "error: domain");
    check(same(err.message(), "access denied"), "error: message");
    check(err == se::errc::permission_denied, "error: errc");

    int port = -1;
    se::system_code r = ParsePort("8080", port);
    check(r.success() && port == 8080, "ParsePort 8080");
    r = ParsePort("http", port);
    check(r == se::errc::invalid_argument && port == 8080,
          "ParsePort http: invalid_argument");
    check(same(r.message(), "Invalid argument"), "ParsePort http: message");
    r = ParsePort("70000", port);
    check(r == se::errc::result_out_of_range, "ParsePort 70000: out of range");
    check(same(r.message(), "Numerical result out of range"),
          "ParsePort 70000: message");
  }

  // throw_exception goes through the domain to a typed status_error.
  {
    bool caught = false;
    try {
      FaultCode(Fault::timeout).throw_exception();
    } catch (const se::status_error<FaultDomain>& e) {
      caught = true;
      check(e.code().value() == Fault::timeout, "status_error: value");
      check(std::strcmp(e.what(), "timed out") == 0, "status_error: what");
      const se::status_error<void>& erased = e;
      check(erased.code() == se::errc::timed_out, "status_error: errc");
    }
    check(caught, "status_error: caught");
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("status-code consumer test passed");
  return 0;
}
