#include "clockUtils/sockets/TcpSocket.h"
#include "clockUtils/sockets/UdpSocket.h"
#include "clockUtils/errors.h"

#include <cstdio>
#include <string>

static int failures;

#define EXPECT(cond)                                                \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

int main() {
  using clockUtils::ClockError;
  namespace sockets = clockUtils::sockets;

  // Only argument checks: no socket is opened.
  sockets::TcpSocket tcp;
  const sockets::IPv4 ip = sockets::convertIP(std::string("10.1.2.3"));
  EXPECT(sockets::convertIP(ip) == "10.1.2.3");
  EXPECT(tcp.connect(ip, 0, 100) == ClockError::INVALID_PORT);
  EXPECT(tcp.connect(sockets::NO_IP, 80, 100) == ClockError::INVALID_IP);
  EXPECT(tcp.writePacket(std::string("x")) == ClockError::NOT_READY);

  sockets::UdpSocket udp;
  EXPECT(udp.writePacket(ip, 4242, std::string("x")) == ClockError::NOT_READY);
  return failures != 0;
}
