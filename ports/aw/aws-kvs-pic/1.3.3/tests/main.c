#include <com/amazonaws/kinesis/video/heap/Include.h>
#include <com/amazonaws/kinesis/video/mkvgen/Include.h>
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
  PHeap heap = NULL;
  ALLOCATION_HANDLE handle = 0;
  UINT64 size;
  PVOID mapped = NULL;
  BYTE cpd[KVS_PCM_CPD_SIZE_BYTE];
  static const BYTE mulawCpd[KVS_PCM_CPD_SIZE_BYTE] = {0x07, 0x00, 0x01, 0x00, 0x40, 0x1f, 0x00, 0x00, 0xe8,
                                                      0x03, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};

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

  /* Heap: the system heap allocates, maps and frees, and rejects a zero-size request. */
  EXPECT(heapInitialize(MIN_HEAP_SIZE, 20, FLAGS_USE_SYSTEM_HEAP, NULL, &heap) == STATUS_SUCCESS);
  EXPECT(heapAlloc(heap, 1024, &handle) == STATUS_SUCCESS && IS_VALID_ALLOCATION_HANDLE(handle));
  EXPECT(heapGetAllocSize(heap, handle, &size) == STATUS_SUCCESS && size == 1024);
  EXPECT(heapMap(heap, handle, &mapped, &size) == STATUS_SUCCESS && size == 1024);
  memset(mapped, 0x5a, 1024);
  EXPECT(heapUnmap(heap, mapped) == STATUS_SUCCESS);
  EXPECT(heap->numAlloc == 1);
  EXPECT(heapFree(heap, handle) == STATUS_SUCCESS && heap->numAlloc == 0);
  EXPECT(heapAlloc(heap, 0, &handle) == STATUS_INVALID_ARG);
  EXPECT(heapRelease(heap) == STATUS_SUCCESS);

  /* MKV generator: the mu-law codec private data is a fixed little-endian WAVEFORMATEX. */
  memset(cpd, 0xff, sizeof(cpd));
  EXPECT(mkvgenGeneratePcmCpd(KVS_PCM_FORMAT_CODE_MULAW, 8000, 1, cpd, sizeof(cpd)) == STATUS_SUCCESS);
  EXPECT(memcmp(cpd, mulawCpd, sizeof(cpd)) == 0);
  EXPECT(mkvgenGeneratePcmCpd(KVS_PCM_FORMAT_CODE_MULAW, 100, 1, cpd, sizeof(cpd)) == STATUS_INVALID_ARG);

  if (failures == 0) {
    puts("ok");
  }
  return failures != 0;
}
