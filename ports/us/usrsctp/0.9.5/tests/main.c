/* Drives usrsctp from C with no threads of the test's own and no network:
   the CRC32c of the standard check string, sysctl setters and their range
   check, and an association between two AF_CONN endpoints whose packets the
   test hands back to the stack, carrying one message each way before a
   graceful shutdown that lets usrsctp_finish succeed. */
#include <usrsctp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int ok, int line, const char *what)
{
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

/* Each endpoint's AF_CONN address is the address of one of these. */
static char client_addr, server_addr;

struct packet {
  void *to;
  void *data;
  size_t length;
};

#define QUEUE_SIZE 256
static struct packet queue[QUEUE_SIZE];
static size_t queue_head, queue_tail;
static int queue_overflow;

/* What one endpoint sends arrives at the other. */
static int conn_output(void *addr, void *buffer, size_t length, uint8_t tos, uint8_t set_df)
{
  struct packet *p;

  (void)tos;
  (void)set_df;
  if (queue_tail == QUEUE_SIZE) {
    queue_overflow = 1;
    return ENOMEM;
  }
  p = &queue[queue_tail];
  if ((p->data = malloc(length)) == NULL) {
    queue_overflow = 1;
    return ENOMEM;
  }
  memcpy(p->data, buffer, length);
  p->length = length;
  p->to = addr == &client_addr ? &server_addr : &client_addr;
  ++queue_tail;
  return 0;
}

/* Delivers the queued packets, and those they cause, until none is left. */
static void pump(void)
{
  while (queue_head < queue_tail) {
    struct packet p = queue[queue_head++];

    usrsctp_conninput(p.to, p.data, p.length, 0);
    free(p.data);
  }
  queue_head = queue_tail = 0;
}

static struct sockaddr_conn conn_address(void *addr)
{
  struct sockaddr_conn sconn;

  memset(&sconn, 0, sizeof sconn);
#if defined(__APPLE__) || defined(__DragonFly__) || defined(__FreeBSD__) || \
    defined(__OpenBSD__) || defined(__NetBSD__)
  sconn.sconn_len = sizeof sconn;
#endif
  sconn.sconn_family = AF_CONN;
  sconn.sconn_port = htons(5001);
  sconn.sconn_addr = addr;
  return sconn;
}

static int send_message(struct socket *so, const char *text, uint16_t sid, uint32_t ppid)
{
  struct sctp_sndinfo sndinfo;

  memset(&sndinfo, 0, sizeof sndinfo);
  sndinfo.snd_sid = sid;
  sndinfo.snd_ppid = htonl(ppid);
  return usrsctp_sendv(so, text, strlen(text), NULL, 0, &sndinfo, (socklen_t)sizeof sndinfo,
                       SCTP_SENDV_SNDINFO, 0) == (ssize_t)strlen(text);
}

static void expect_message(struct socket *so, const char *text, uint16_t sid, uint32_t ppid)
{
  char buffer[64];
  struct sctp_rcvinfo rcvinfo;
  socklen_t infolen = (socklen_t)sizeof rcvinfo;
  unsigned int infotype = 0;
  int flags = 0;
  ssize_t n;

  memset(&rcvinfo, 0, sizeof rcvinfo);
  n = usrsctp_recvv(so, buffer, sizeof buffer, NULL, NULL, &rcvinfo, &infolen, &infotype, &flags);
  CHECK(n == (ssize_t)strlen(text));
  CHECK(n > 0 && memcmp(buffer, text, (size_t)n) == 0);
  CHECK(infotype == SCTP_RECVV_RCVINFO);
  CHECK(rcvinfo.rcv_sid == sid);
  CHECK(ntohl(rcvinfo.rcv_ppid) == ppid);
  CHECK((flags & MSG_EOR) != 0 && (flags & MSG_NOTIFICATION) == 0);
}

int main(void)
{
  /* CRC-32C's check value 0xE3069283, in the byte order SCTP sends it. */
  static const unsigned char check_crc[4] = {0x83, 0x92, 0x06, 0xe3};
  char check_string[] = "123456789";
  uint32_t crc;
  struct socket *client, *listener, *server;
  struct sockaddr_conn sconn;
  const int on = 1;
  int i, finished = 0;

  usrsctp_init_nothreads(0, conn_output, NULL);

  crc = usrsctp_crc32c(check_string, strlen(check_string));
  CHECK(memcmp(&crc, check_crc, sizeof crc) == 0);

  CHECK(usrsctp_sysctl_get_sctp_ecn_enable() == 1);
  CHECK(usrsctp_sysctl_set_sctp_ecn_enable(0) == 0);
  CHECK(usrsctp_sysctl_get_sctp_ecn_enable() == 0);
  errno = 0;
  CHECK(usrsctp_sysctl_set_sctp_ecn_enable(2) == -1 && errno == EINVAL);
  CHECK(usrsctp_sysctl_get_sctp_ecn_enable() == 0);
  CHECK(usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL) == 0);
  CHECK(usrsctp_sysctl_get_sctp_debug_on() == SCTP_DEBUG_ALL);
  CHECK(usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_NONE) == 0);

  usrsctp_register_address(&client_addr);
  usrsctp_register_address(&server_addr);
  client = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL);
  listener = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL);
  if (client == NULL || listener == NULL) {
    fprintf(stderr, "main.c: usrsctp_socket failed: errno %d\n", errno);
    return 1;
  }
  CHECK(usrsctp_set_non_blocking(client, 1) == 0);
  CHECK(usrsctp_set_non_blocking(listener, 1) == 0);
  CHECK(usrsctp_setsockopt(client, IPPROTO_SCTP, SCTP_RECVRCVINFO, &on, sizeof on) == 0);
  sconn = conn_address(&client_addr);
  CHECK(usrsctp_bind(client, (struct sockaddr *)&sconn, sizeof sconn) == 0);
  sconn = conn_address(&server_addr);
  CHECK(usrsctp_bind(listener, (struct sockaddr *)&sconn, sizeof sconn) == 0);
  CHECK(usrsctp_listen(listener, 1) == 0);

  /* The handshake waits for the test to deliver its packets. */
  sconn = conn_address(&client_addr);
  errno = 0;
  CHECK(usrsctp_connect(client, (struct sockaddr *)&sconn, sizeof sconn) == -1 &&
        errno == EINPROGRESS);
  pump();
  server = usrsctp_accept(listener, NULL, NULL);
  if (server == NULL) {
    fprintf(stderr, "main.c: usrsctp_accept failed: errno %d\n", errno);
    return 1;
  }
  CHECK(usrsctp_set_non_blocking(server, 1) == 0);
  CHECK(usrsctp_setsockopt(server, IPPROTO_SCTP, SCTP_RECVRCVINFO, &on, sizeof on) == 0);

  CHECK(send_message(client, "hello", 1, 42));
  pump();
  expect_message(server, "hello", 1, 42);
  CHECK(send_message(server, "world!", 3, 7));
  pump();
  expect_message(client, "world!", 3, 7);

  usrsctp_close(client);
  usrsctp_close(server);
  usrsctp_close(listener);
  usrsctp_deregister_address(&client_addr);
  usrsctp_deregister_address(&server_addr);
  /* The shutdown runs on the packets the test delivers and on timers that
     advance only through usrsctp_handle_timers. */
  for (i = 0; i < 1000 && !finished; ++i) {
    pump();
    usrsctp_handle_timers(10);
    finished = usrsctp_finish() == 0;
  }
  CHECK(finished);
  CHECK(!queue_overflow);
  while (queue_head < queue_tail) {
    free(queue[queue_head++].data);
  }

  if (failures != 0) {
    fprintf(stderr, "main.c: %d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
