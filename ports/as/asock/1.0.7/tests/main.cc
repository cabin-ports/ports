#include "asock/asock_tcp_client.hpp"
#include "asock/asock_tcp_server.hpp"
#include "asock/asock_udp_client.hpp"
#include "asock/asock_udp_server.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

// The receive buffer: fixed mode refuses what does not fit, auto-growing mode
// doubles, and reads wrap around the end of the ring.
static void buffer() {
  CumBuffer fixed;
  CHECK(fixed.Init(8) == cumbuffer::OP_RSLT_OK);
  char abcdef[] = "abcdef";
  CHECK(fixed.Append(6, abcdef) == cumbuffer::OP_RSLT_OK);
  char out[16] = {};
  CHECK(fixed.GetData(4, out) == cumbuffer::OP_RSLT_OK);
  CHECK(std::memcmp(out, "abcd", 4) == 0);
  CHECK(fixed.GetCumulatedLen() == 2);
  char ghij[] = "ghij";
  CHECK(fixed.Append(4, ghij) == cumbuffer::OP_RSLT_OK);  // wraps around
  CHECK(fixed.GetCumulatedLen() == 6);
  CHECK(fixed.GetData(6, out) == cumbuffer::OP_RSLT_OK);
  CHECK(std::memcmp(out, "efghij", 6) == 0);
  char full[] = "0123456789";
  CHECK(fixed.Append(8, full) == cumbuffer::OP_RSLT_OK);
  CHECK(fixed.Append(1, full) == cumbuffer::OP_RSLT_BUFFER_FULL);
  CHECK(fixed.GetData(9, out) != cumbuffer::OP_RSLT_OK);

  CumBuffer growing;
  CHECK(growing.InitAutoGrowing(8) == cumbuffer::OP_RSLT_OK);
  CHECK(growing.Append(8, full) == cumbuffer::OP_RSLT_OK);
  CHECK(growing.Append(2, full + 8) == cumbuffer::OP_RSLT_OK);
  CHECK(growing.GetCapacity() == 16);
  CHECK(growing.GetCumulatedLen() == 10);
  CHECK(growing.GetData(10, out) == cumbuffer::OP_RSLT_OK);
  CHECK(std::memcmp(out, "0123456789", 10) == 0);
  CHECK(growing.GetCumulatedLen() == 0);
}

// Behaviour that needs no socket: a server refuses a zero event limit, a
// callback must not be null, and a client that never connected cannot send.
// The wire header is ten characters, and the default buffer holds 1400 bytes
// beyond it.
static void api() {
  CHECK(sizeof(asock::ST_HEADER) == 10);
  CHECK(asock::DEFAULT_BUFFER_SIZE == 10 + 1400);

  asock::ASockTcpServer tcp_server;
  CHECK(!tcp_server.RunTcpServer("127.0.0.1", 19907, 1024, 0));
  CHECK(tcp_server.GetLastErrMsg() == "max event is 0");
  CHECK(!tcp_server.SetCbOnClientConnected(nullptr));
  CHECK(tcp_server.GetLastErrMsg() == "callback is null");
  CHECK(!tcp_server.SetCbOnClientDisconnected(nullptr));
  CHECK(!tcp_server.IsServerRunning());
  CHECK(tcp_server.GetCountOfClients() == 0);

  asock::ASockUdpServer udp_server;
  CHECK(!udp_server.RunUdpServer("127.0.0.1", 19907, 1024, 0));
  CHECK(udp_server.GetLastErrMsg() == "max event is 0");

  asock::ASockTcpClient tcp_client;
  CHECK(!tcp_client.IsConnected());
  CHECK(!tcp_client.SendToServer("x", 1));
  CHECK(tcp_client.GetLastErrMsg() == "not connected");
  CHECK(!tcp_client.SetCbOnDisconnectedFromServer(nullptr));
  CHECK(tcp_client.GetLastErrMsg() == "callback is null");
  CHECK(!tcp_client.SetCbOnRecvedCompletePacket(nullptr));

  asock::ASockUdpClient udp_client;
  CHECK(!udp_client.SendToServer("x", 1));
  CHECK(udp_client.GetLastErrMsg() == "not connected");
}

int main() {
  buffer();
  api();
  if (failures) return 1;
  std::puts("ok");
  return 0;
}
