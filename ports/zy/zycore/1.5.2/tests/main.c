/* C consumer: version, string formatting, vector, list, bitset, critical
 * section and thread id. */
#include <Zycore/API/Memory.h>
#include <Zycore/API/Synchronization.h>
#include <Zycore/API/Thread.h>
#include <Zycore/Bitset.h>
#include <Zycore/Format.h>
#include <Zycore/List.h>
#include <Zycore/String.h>
#include <Zycore/Vector.h>
#include <Zycore/Zycore.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char* what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

int main(void) {
  ZyanU64 version = ZycoreGetVersion();
  CHECK(ZYCORE_VERSION_MAJOR(version) == 1);
  CHECK(ZYCORE_VERSION_MINOR(version) == 5);
  CHECK(ZYCORE_VERSION_PATCH(version) == 2);

  ZyanString s;
  const char* data = NULL;
  ZyanUSize size = 0;
  CHECK(ZYAN_SUCCESS(ZyanStringInit(&s, 16)));
  CHECK(ZYAN_SUCCESS(ZyanStringAppendFormat(&s, "%s-%d", "zy", 42)));
  CHECK(ZYAN_SUCCESS(ZyanStringAppendHexU(&s, 0xBEEF, 0, ZYAN_TRUE)));
  CHECK(ZYAN_SUCCESS(ZyanStringAppendDecS(&s, -7, 0, ZYAN_FALSE, ZYAN_NULL)));
  CHECK(ZYAN_SUCCESS(ZyanStringGetData(&s, &data)));
  CHECK(data && strcmp(data, "zy-42BEEF-7") == 0);
  CHECK(ZYAN_SUCCESS(ZyanStringGetSize(&s, &size)) && size == 11);
  ZyanStringDestroy(&s);
  CHECK(ZyanStringGetSize(ZYAN_NULL, &size) == ZYAN_STATUS_INVALID_ARGUMENT);
  CHECK(ZYAN_FAILED(ZyanStringGetSize(ZYAN_NULL, &size)) != 0);

  ZyanVector v;
  int values[] = {3, 1, 2};
  CHECK(ZYAN_SUCCESS(ZyanVectorInit(&v, sizeof(int), 4, ZYAN_NULL)));
  for (int i = 0; i < 3; ++i) CHECK(ZYAN_SUCCESS(ZyanVectorPushBack(&v, &values[i])));
  CHECK(ZYAN_SUCCESS(ZyanVectorGetSize(&v, &size)) && size == 3);
  CHECK(*(const int*)ZyanVectorGet(&v, 0) == 3);
  CHECK(*(const int*)ZyanVectorGet(&v, 1) == 1);
  CHECK(*(const int*)ZyanVectorGet(&v, 2) == 2);
  ZyanVectorDestroy(&v);

  ZyanList list;
  const ZyanListNode* node = NULL;
  int sum = 0, count = 0;
  CHECK(ZYAN_SUCCESS(ZyanListInit(&list, sizeof(int), ZYAN_NULL)));
  for (int i = 0; i < 3; ++i) CHECK(ZYAN_SUCCESS(ZyanListPushBack(&list, &values[i])));
  CHECK(ZYAN_SUCCESS(ZyanListGetSize(&list, &size)) && size == 3);
  CHECK(ZYAN_SUCCESS(ZyanListGetHeadNode(&list, &node)));
  while (node) {
    sum = sum * 10 + *(const int*)ZyanListGetNodeData(node);
    ++count;
    ZyanListGetNextNode(&node);
  }
  CHECK(count == 3 && sum == 312);
  ZyanListDestroy(&list);

  ZyanBitset bits;
  CHECK(ZYAN_SUCCESS(ZyanBitsetInit(&bits, 64)));
  CHECK(ZYAN_SUCCESS(ZyanBitsetSet(&bits, 1)));
  CHECK(ZYAN_SUCCESS(ZyanBitsetSet(&bits, 63)));
  CHECK(ZyanBitsetTest(&bits, 63) == ZYAN_STATUS_TRUE);
  CHECK(ZyanBitsetTest(&bits, 2) == ZYAN_STATUS_FALSE);
  CHECK(ZYAN_SUCCESS(ZyanBitsetCount(&bits, &size)) && size == 2);
  ZyanBitsetDestroy(&bits);

  ZyanCriticalSection cs;
  CHECK(ZYAN_SUCCESS(ZyanCriticalSectionInitialize(&cs)));
  CHECK(ZYAN_SUCCESS(ZyanCriticalSectionEnter(&cs)));
  CHECK(ZyanCriticalSectionTryEnter(&cs));  /* recursive */
  CHECK(ZYAN_SUCCESS(ZyanCriticalSectionLeave(&cs)));
  CHECK(ZYAN_SUCCESS(ZyanCriticalSectionLeave(&cs)));
  CHECK(ZYAN_SUCCESS(ZyanCriticalSectionDelete(&cs)));

  ZyanThreadId tid;
  CHECK(ZYAN_SUCCESS(ZyanThreadGetCurrentThreadId(&tid)));
  CHECK(ZyanMemoryGetSystemPageSize() > 0);

  if (failures) return 1;
  puts("ok");
  return 0;
}
