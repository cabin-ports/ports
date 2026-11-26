#include <ace/ACE.h>
#include <ace/Codecs.h>
#include <ace/Get_Opt.h>
#include <ace/INET_Addr.h>
#include <ace/Init_ACE.h>
#include <ace/Message_Block.h>
#include <ace/Message_Queue.h>
#include <ace/SString.h>
#include <ace/Synch_Traits.h>
#include <ace/Thread_Manager.h>
#include <ace/Time_Value.h>
#include <ace/UUID.h>

#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

using Queue = ACE_Message_Queue<ACE_MT_SYNCH>;

static size_t consumed_bytes = 0;

// Sums the lengths of the blocks it dequeues until a hangup block.
static ACE_THR_FUNC_RETURN consume(void* arg) {
  Queue* queue = static_cast<Queue*>(arg);
  for (;;) {
    ACE_Message_Block* mb = nullptr;
    if (queue->dequeue_head(mb) == -1) break;
    const bool hangup = mb->msg_type() == ACE_Message_Block::MB_HANGUP;
    consumed_bytes += mb->length();
    mb->release();
    if (hangup) break;
  }
  return 0;
}

static void test_addr() {
  ACE_INET_Addr addr("127.0.0.1:8080");
  CHECK(addr.get_port_number() == 8080);
  CHECK(addr.get_ip_address() == 0x7F000001u);
}

static void test_time() {
  ACE_Time_Value t(1, 500000);
  t += ACE_Time_Value(0, 700000);
  CHECK(t.sec() == 2);
  CHECK(t.usec() == 200000);
  CHECK(t.msec() == 2200);
}

static void test_message_block() {
  ACE_Message_Block mb(64);
  CHECK(mb.copy("hello", 5) == 0);
  CHECK(mb.length() == 5);
  CHECK(mb.space() == 59);
  CHECK(std::memcmp(mb.rd_ptr(), "hello", 5) == 0);
}

static void test_string() {
  ACE_CString s("abc");
  s += "def";
  CHECK(s.length() == 6);
  CHECK(s.find("de") == 3);
  CHECK(s == "abcdef");
}

static void test_get_opt() {
  char a0[] = "prog", a1[] = "-a", a2[] = "-b", a3[] = "val";
  char* argv[] = {a0, a1, a2, a3, nullptr};
  ACE_Get_Opt opts(4, argv, "ab:");
  CHECK(opts() == 'a');
  CHECK(opts() == 'b');
  CHECK(opts.opt_arg() != nullptr && std::strcmp(opts.opt_arg(), "val") == 0);
  CHECK(opts() == EOF);
}

static void test_crc_and_base64() {
  CHECK(ACE::crc32("123456789") == 0xCBF43926u);

  size_t len = 0;
  ACE_Byte* encoded = ACE_Base64::encode(
      reinterpret_cast<const ACE_Byte*>("hello"), 5, &len, false);
  CHECK(encoded != nullptr && len == 8 &&
        std::memcmp(encoded, "aGVsbG8=", 8) == 0);
  size_t decoded_len = 0;
  ACE_Byte* decoded = ACE_Base64::decode(encoded, &decoded_len);
  CHECK(decoded != nullptr && decoded_len == 5 &&
        std::memcmp(decoded, "hello", 5) == 0);
  delete[] decoded;
  delete[] encoded;
}

static void test_uuid() {
  ACE_Utils::UUID uuid(ACE_CString("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
  CHECK(uuid.time_low() == 0x6ba7b810u);
  CHECK(uuid.time_hi_and_version() == 0x11d1u);
  CHECK(*uuid.to_string() == "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
}

static void test_thread_queue() {
  Queue queue;
  CHECK(ACE_Thread_Manager::instance()->spawn(consume, &queue) != -1);
  for (const char* word : {"one", "three", "seven"}) {
    ACE_Message_Block* mb = new ACE_Message_Block(16);
    mb->copy(word, std::strlen(word));
    CHECK(queue.enqueue_tail(mb) != -1);
  }
  CHECK(queue.enqueue_tail(new ACE_Message_Block(
            0, ACE_Message_Block::MB_HANGUP)) != -1);
  CHECK(ACE_Thread_Manager::instance()->wait() == 0);
  CHECK(consumed_bytes == 13);
}

// ace/OS_main.h renames main on Windows and calls it with (argc, argv).
int main(int, char*[]) {
  if (ACE::init() == -1) return 1;
  test_addr();
  test_time();
  test_message_block();
  test_string();
  test_get_opt();
  test_crc_and_base64();
  test_uuid();
  test_thread_queue();
  ACE::fini();
  if (failures != 0) return 1;
  std::puts("ok");
  return 0;
}
