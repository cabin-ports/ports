#include <com/amazonaws/kinesis/video/utils/Include.h>

#include <cstdio>
#include <cstring>

static int failures;

#define EXPECT(cond)                                           \
  do {                                                        \
    if (!(cond)) {                                            \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
      ++failures;                                             \
    }                                                         \
  } while (0)

int main() {
  char text[] = "Zm9vYmFy";
  char out[32];
  BYTE bytes[32];
  UINT32 len = sizeof(bytes), count = 0;
  UINT64 item = 0;
  INT64 number = 0;

  EXPECT(base64Decode(text, 8, bytes, &len) == STATUS_SUCCESS);
  EXPECT(len == 6 && std::memcmp(bytes, "foobar", 6) == 0);

  EXPECT(ulltostr(1234567890123ULL, out, sizeof(out), 10, &len) == STATUS_SUCCESS);
  EXPECT(len == 13 && std::strcmp(out, "1234567890123") == 0);
  EXPECT(ulltostr(255, out, sizeof(out), 16, &len) == STATUS_SUCCESS);
  EXPECT(std::strcmp(out, "ff") == 0);
  char negative[] = "-42";
  EXPECT(strtoi64(negative, NULL, 10, &number) == STATUS_SUCCESS && number == -42);

  PDoubleList list = NULL;
  EXPECT(doubleListCreate(&list) == STATUS_SUCCESS);
  EXPECT(doubleListInsertItemTail(list, 2) == STATUS_SUCCESS);
  EXPECT(doubleListInsertItemTail(list, 3) == STATUS_SUCCESS);
  EXPECT(doubleListInsertItemHead(list, 1) == STATUS_SUCCESS);
  EXPECT(doubleListGetNodeCount(list, &count) == STATUS_SUCCESS && count == 3);
  EXPECT(doubleListGetNodeDataAt(list, 0, &item) == STATUS_SUCCESS && item == 1);
  EXPECT(doubleListGetNodeDataAt(list, 2, &item) == STATUS_SUCCESS && item == 3);
  EXPECT(doubleListDeleteHead(list) == STATUS_SUCCESS);
  EXPECT(doubleListGetNodeDataAt(list, 0, &item) == STATUS_SUCCESS && item == 2);
  EXPECT(doubleListFree(list) == STATUS_SUCCESS);

  if (failures == 0) {
    std::puts("ok");
  }
  return failures != 0;
}
