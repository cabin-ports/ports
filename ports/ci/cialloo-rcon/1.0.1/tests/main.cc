#include <rcon.hpp>

#include <cstdio>
#include <string>

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::fprintf(stderr, "FAIL: %s\n", what);
      ++failures;
    }
  };

  unsigned char word[4] = {0x01, 0x02, 0x03, 0x04};
  check(valve::byte32_to_int(word) == 0x04030201, "little-endian 0x04030201");
  unsigned char id[4] = {0x2a, 0x00, 0x00, 0x00};
  check(valve::byte32_to_int(id) == 42, "little-endian 42");
  unsigned char zero[4] = {0, 0, 0, 0};
  check(valve::byte32_to_int(zero) == 0, "zero");
  check(std::string(RCON_VERSION) == "1.0.1", "RCON_VERSION");
  check(SERVERDATA_AUTH == 3 && SERVERDATA_EXECCOMMAND == 2, "packet types");

  // A service name that does not resolve fails before any connection.
  bool threw = false;
  try {
    valve::rcon("127.0.0.1", "no-such-service", "pw", "status");
  } catch (const asio::system_error&) {
    threw = true;
  }
  check(threw, "rcon throws asio::system_error on an unresolvable service");

  return failures == 0 ? 0 : 1;
}
