// Parses digits and reports failures through neko-schema's exception
// hierarchy: source locations captured where the error is thrown, the
// logic/runtime split, default messages, and the nested exception every
// neko::ex::Exception carries.  Also checks the type aliases, the enums
// and their toString.  C++20, the floor of srcLoc.hpp's <source_location>.
#include <neko/schema/exception.hpp>
#include <neko/schema/srcLoc.hpp>
#include <neko/schema/types.hpp>

#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

bool contains(neko::cstr text, std::string_view part) {
  return text != nullptr && std::string_view(text).find(part) != std::string_view::npos;
}

bool ends_with(neko::cstr text, std::string_view suffix) {
  return text != nullptr && std::string_view(text).ends_with(suffix);
}

static_assert(std::is_same_v<neko::types::uint32, neko::uint32> && sizeof(neko::uint8) == 1 &&
              sizeof(neko::int16) == 2 && sizeof(neko::uint64) == 8 &&
              std::is_same_v<neko::strview, std::string_view>);
static_assert(std::is_same_v<std::underlying_type_t<neko::Priority>, neko::uint8>);
static_assert(std::is_base_of_v<neko::ex::SystemError, neko::ex::DatabaseError> &&
              std::is_base_of_v<neko::ex::RuntimeError, neko::ex::SystemError> &&
              std::is_base_of_v<neko::ex::ConcurrencyError, neko::ex::TaskRejectedError> &&
              std::is_base_of_v<std::nested_exception, neko::ex::Exception> &&
              !std::is_base_of_v<neko::ex::LogicError, neko::ex::RuntimeError>);

// A manually built location is usable at compile time.
constexpr neko::SrcLocInfo fixed("config.toml", 12, "load");
static_assert(fixed.hasInfo() && fixed.getLine() == 12);
static_assert(!neko::SrcLocInfo(nullptr, 0, nullptr).hasInfo());

neko::uint32 throw_line = 0;

int digit(char c) {
  if (c < '0' || c > '9') {
    throw_line = __LINE__ + 1;
    throw neko::ex::RangeError(std::string("not a digit: ") + c, neko::SrcLocInfo{});
  }
  return c - '0';
}

int parse(std::string_view text) {
  if (text.empty()) throw neko::ex::ArgumentError("empty number");
  int value = 0;
  for (char c : text) value = value * 10 + digit(c);
  return value;
}

}  // namespace

int main() {
  check(neko::toString(neko::State::RetryRequired) == std::string_view("RetryRequired") &&
            neko::toString(neko::Priority::Critical) == std::string_view("Critical") &&
            neko::toString(static_cast<neko::Priority>(9)) == std::string_view("Unknown") &&
            static_cast<int>(neko::SyncMode::Async) == 1,
        "enums and toString");

  // A default-constructed location is where it is written.
  const neko::SrcLocInfo here;
  const neko::uint32 here_line = __LINE__ - 1;
  check(here.hasInfo() && here.getLine() == here_line &&
            ends_with(here.getFile(), "main.cc") && contains(here.getFunc(), "main"),
        "SrcLocInfo captures the current location");

  check(parse("2026") == 2026, "parse");

  // The thrown RangeError is caught as each of its bases.
  try {
    parse("20x6");
    check(false, "a non-digit throws");
  } catch (const neko::ex::LogicError& e) {
    check(dynamic_cast<const neko::ex::RangeError*>(&e) != nullptr &&
              dynamic_cast<const neko::ex::ArgumentError*>(&e) != nullptr &&
              dynamic_cast<const neko::ex::RuntimeError*>(&e) == nullptr,
          "RangeError is an ArgumentError and a LogicError, not a RuntimeError");
    check(std::string_view(e.what()) == "not a digit: x" && e.getMessage() == "not a digit: x",
          "the message");
    check(e.hasSrcLocInfo() && e.getLine() == throw_line && contains(e.getFunc(), "digit") &&
              ends_with(e.getFile(), "main.cc"),
          "the location where digit threw");
  }

  try {
    parse("");
    check(false, "an empty number throws");
  } catch (const std::exception& e) {
    check(std::string_view(e.what()) == "empty number", "caught as std::exception");
  }

  // Each neko exception captures the exception being handled when it is
  // constructed, so a higher layer can wrap a lower one.
  try {
    try {
      parse("12a");
    } catch (const neko::ex::Exception&) {
      throw neko::ex::ConfigurationError("port is not a number");
    }
  } catch (const neko::ex::RuntimeError& outer) {
    check(std::string_view(outer.what()) == "port is not a number" && outer.nested_ptr() != nullptr,
          "the configuration error wraps the parse error");
    // rethrow_nested on an empty nested_ptr would call std::terminate.
    if (outer.nested_ptr() != nullptr) {
      try {
        outer.rethrow_nested();
      } catch (const neko::ex::RangeError& inner) {
        check(std::string_view(inner.what()) == "not a digit: a", "rethrow_nested gives the cause");
      } catch (...) {
        check(false, "rethrow_nested gives a RangeError");
      }
    }
  }

  // Default messages, and a null C string falling back to them.
  check(std::string_view(neko::ex::TimeoutError().what()) == "Timeout!" &&
            std::string_view(neko::ex::FileError().what()) == "File error!" &&
            std::string_view(neko::ex::ProgramExit().what()) == "Program exited!" &&
            std::string_view(neko::ex::LogicError(static_cast<neko::cstr>(nullptr)).what()) ==
                "Logic error!" &&
            neko::ex::FileError().nested_ptr() == nullptr,
        "default messages");
  const neko::ex::DatabaseError db("connection lost", fixed);
  check(db.getLine() == 12 && std::string_view(db.getFile()) == "config.toml" &&
            std::string_view(db.getFunc()) == "load",
        "an explicit location");

  std::printf("neko-schema (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
