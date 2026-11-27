// Drives usrsctp from C++ with no threads of the test's own and no network:
// an association between two AF_CONN endpoints whose packets the test hands
// back to the stack, with the stream counts SCTP_INITMSG asks for and
// SCTP_STATUS reports, a read upcall for the message the client sends, and
// a graceful shutdown that lets usrsctp_finish succeed.
#include <usrsctp.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char *what)
{
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

// Each endpoint's AF_CONN address is the address of one of these.
char client_addr, server_addr;

struct Packet {
  void *to;
  std::vector<unsigned char> data;
};

std::deque<Packet> queue;

// What one endpoint sends arrives at the other.
int conn_output(void *addr, void *buffer, size_t length, uint8_t, uint8_t)
{
  const unsigned char *bytes = static_cast<const unsigned char *>(buffer);
  Packet packet;
  packet.to = addr == &client_addr ? &server_addr : &client_addr;
  packet.data.assign(bytes, bytes + length);
  queue.push_back(packet);
  return 0;
}

// Delivers the queued packets, and those they cause, until none is left.
void pump()
{
  while (!queue.empty()) {
    Packet packet = queue.front();
    queue.pop_front();
    usrsctp_conninput(packet.to, &packet.data[0], packet.data.size(), 0);
  }
}

sockaddr_conn conn_address(void *addr)
{
  sockaddr_conn sconn;
  std::memset(&sconn, 0, sizeof sconn);
#if defined(__APPLE__) || defined(__DragonFly__) || defined(__FreeBSD__) || \
    defined(__OpenBSD__) || defined(__NetBSD__)
  sconn.sconn_len = sizeof sconn;
#endif
  sconn.sconn_family = AF_CONN;
  sconn.sconn_port = htons(5001);
  sconn.sconn_addr = addr;
  return sconn;
}

sctp_status status_of(struct socket *so)
{
  sctp_status status;
  std::memset(&status, 0, sizeof status);
  socklen_t length = sizeof status;
  CHECK(usrsctp_getsockopt(so, IPPROTO_SCTP, SCTP_STATUS, &status, &length) == 0);
  return status;
}

int read_events = 0;

void on_event(struct socket *so, void *, int)
{
  if (usrsctp_get_events(so) & SCTP_EVENT_READ) {
    ++read_events;
  }
}

}  // namespace

int main()
{
  usrsctp_init_nothreads(0, conn_output, NULL);
  usrsctp_sysctl_set_sctp_ecn_enable(0);
  usrsctp_register_address(&client_addr);
  usrsctp_register_address(&server_addr);

  struct socket *client = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL);
  struct socket *listener = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL);
  if (client == NULL || listener == NULL) {
    std::fprintf(stderr, "main.cc: usrsctp_socket failed: errno %d\n", errno);
    return 1;
  }
  CHECK(usrsctp_set_non_blocking(client, 1) == 0);
  CHECK(usrsctp_set_non_blocking(listener, 1) == 0);
  sctp_initmsg initmsg;
  std::memset(&initmsg, 0, sizeof initmsg);
  initmsg.sinit_num_ostreams = 5;
  CHECK(usrsctp_setsockopt(client, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof initmsg) == 0);
  sockaddr_conn sconn = conn_address(&client_addr);
  CHECK(usrsctp_bind(client, reinterpret_cast<sockaddr *>(&sconn), sizeof sconn) == 0);
  sconn = conn_address(&server_addr);
  CHECK(usrsctp_bind(listener, reinterpret_cast<sockaddr *>(&sconn), sizeof sconn) == 0);
  CHECK(usrsctp_listen(listener, 1) == 0);

  sconn = conn_address(&client_addr);
  errno = 0;
  CHECK(usrsctp_connect(client, reinterpret_cast<sockaddr *>(&sconn), sizeof sconn) == -1 &&
        errno == EINPROGRESS);
  CHECK(status_of(client).sstat_state == SCTP_COOKIE_WAIT);
  pump();
  struct socket *server = usrsctp_accept(listener, NULL, NULL);
  if (server == NULL) {
    std::fprintf(stderr, "main.cc: usrsctp_accept failed: errno %d\n", errno);
    return 1;
  }
  sctp_status client_status = status_of(client);
  CHECK(client_status.sstat_state == SCTP_ESTABLISHED);
  CHECK(client_status.sstat_outstrms == 5);
  CHECK(status_of(server).sstat_instrms == 5);

  CHECK(usrsctp_set_non_blocking(server, 1) == 0);
  CHECK(usrsctp_set_upcall(server, on_event, NULL) == 0);
  sctp_sndinfo sndinfo;
  std::memset(&sndinfo, 0, sizeof sndinfo);
  sndinfo.snd_sid = 4;
  CHECK(usrsctp_sendv(client, "ping", 4, NULL, 0, &sndinfo, sizeof sndinfo, SCTP_SENDV_SNDINFO,
                      0) == 4);
  CHECK(read_events == 0);
  pump();
  CHECK(read_events > 0);
  char buffer[16];
  sctp_rcvinfo rcvinfo;
  socklen_t infolen = sizeof rcvinfo;
  unsigned int infotype = 0;
  int flags = 0;
  CHECK(usrsctp_recvv(server, buffer, sizeof buffer, NULL, NULL, &rcvinfo, &infolen, &infotype,
                      &flags) == 4);
  CHECK(std::memcmp(buffer, "ping", 4) == 0);
  CHECK((flags & MSG_EOR) != 0);
  CHECK(infotype == SCTP_RECVV_NOINFO);
  infolen = sizeof rcvinfo;
  flags = 0;
  errno = 0;
  CHECK(usrsctp_recvv(server, buffer, sizeof buffer, NULL, NULL, &rcvinfo, &infolen, &infotype,
                      &flags) == -1 &&
        errno == EWOULDBLOCK);

  usrsctp_close(client);
  usrsctp_close(server);
  usrsctp_close(listener);
  usrsctp_deregister_address(&client_addr);
  usrsctp_deregister_address(&server_addr);
  // The shutdown runs on the packets the test delivers and on timers that
  // advance only through usrsctp_handle_timers.
  bool finished = false;
  for (int i = 0; i < 1000 && !finished; ++i) {
    pump();
    usrsctp_handle_timers(10);
    finished = usrsctp_finish() == 0;
  }
  CHECK(finished);

  if (failures != 0) {
    std::fprintf(stderr, "main.cc: %d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
