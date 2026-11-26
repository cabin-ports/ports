#include <com/amazonaws/kinesis/video/utils/Include.h>

#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
      ++failures;                                           \
    }                                                       \
  } while (0)

int main(void) {
  char encoded[32], hex[32];
  BYTE decoded[32];
  UINT32 len, count, value;
  UINT64 item;
  PStackQueue queue = NULL;

  /* CRC-32 (ISO-HDLC) check value. */
  EXPECT(COMPUTE_CRC32((PBYTE) "123456789", 9) == 0xCBF43926u);

  /* The reported length counts the terminating NUL. */
  len = sizeof(encoded);
  EXPECT(base64Encode((PVOID) "foobar", 6, encoded, &len) == STATUS_SUCCESS);
  EXPECT(len == 9 && strcmp(encoded, "Zm9vYmFy") == 0);
  len = sizeof(encoded);
  EXPECT(base64Encode((PVOID) "fo", 2, encoded, &len) == STATUS_SUCCESS);
  EXPECT(strcmp(encoded, "Zm8=") == 0);
  len = sizeof(decoded);
  EXPECT(base64Decode("Zm9vYmFy", 8, decoded, &len) == STATUS_SUCCESS);
  EXPECT(len == 6 && memcmp(decoded, "foobar", 6) == 0);

  len = sizeof(hex);
  EXPECT(hexEncode((PVOID) "\x01\xab\xff", 3, hex, &len) == STATUS_SUCCESS);
  EXPECT(len == 7 && strcmp(hex, "01ABFF") == 0);
  len = sizeof(decoded);
  EXPECT(hexDecode(hex, 6, decoded, &len) == STATUS_SUCCESS);
  EXPECT(len == 3 && decoded[0] == 0x01 && decoded[1] == 0xab && decoded[2] == 0xff);

  EXPECT(strtoui32("4294967295", NULL, 10, &value) == STATUS_SUCCESS);
  EXPECT(value == 4294967295u);
  EXPECT(strtoui32("ff", NULL, 16, &value) == STATUS_SUCCESS && value == 255);

  EXPECT(stackQueueCreate(&queue) == STATUS_SUCCESS);
  EXPECT(stackQueueEnqueue(queue, 10) == STATUS_SUCCESS);
  EXPECT(stackQueueEnqueue(queue, 20) == STATUS_SUCCESS);
  EXPECT(stackQueueEnqueue(queue, 30) == STATUS_SUCCESS);
  EXPECT(stackQueueGetCount(queue, &count) == STATUS_SUCCESS && count == 3);
  EXPECT(stackQueueDequeue(queue, &item) == STATUS_SUCCESS && item == 10);
  EXPECT(stackQueueGetIndexOf(queue, 30, &count) == STATUS_SUCCESS && count == 1);
  EXPECT(stackQueueFree(queue) == STATUS_SUCCESS);

  if (failures == 0) {
    puts("ok");
  }
  return failures != 0;
}
