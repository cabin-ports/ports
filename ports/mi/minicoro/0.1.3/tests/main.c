/* Runs coroutines on minicoro's default backend: a generator that yields
 * values through the storage from a nested call, a coroutine resuming another
 * (as upstream's tests/testsuite.c does), a custom allocator and stack size,
 * a caller-allocated coroutine, the storage's limits and LIFO order, and the
 * results of misuse. */
#define MINICORO_IMPL
#include <minicoro.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr)                                                        \
  do {                                                                     \
    if (!(expr)) {                                                         \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #expr);   \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

/* Yields from below the entry function, through mco_running(). */
static void yield_value(int value)
{
  mco_coro *co = mco_running();
  CHECK(mco_push(co, &value, sizeof(value)) == MCO_SUCCESS);
  CHECK(mco_yield(co) == MCO_SUCCESS);
}

static void fibonacci(mco_coro *co)
{
  int count = 0;
  int a = 0;
  int b = 1;
  int i;
  CHECK(mco_running() == co);
  CHECK(mco_status(co) == MCO_RUNNING);
  CHECK(*(int *)mco_get_user_data(co) == 42);
  CHECK(mco_pop(co, &count, sizeof(count)) == MCO_SUCCESS);
  for (i = 0; i < count; ++i) {
    int next = a + b;
    yield_value(a);
    a = b;
    b = next;
  }
}

static void test_generator(void)
{
  static const int expected[10] = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34};
  mco_desc desc = mco_desc_init(fibonacci, 0);
  mco_coro *co = NULL;
  int user_data = 42;
  int count = 10;
  int got[10];
  int i;
  memset(got, 0xff, sizeof(got));
  desc.user_data = &user_data;
  CHECK(mco_create(&co, &desc) == MCO_SUCCESS);
  if (co == NULL)
    return;
  CHECK(mco_status(co) == MCO_SUSPENDED);
  CHECK(mco_running() == NULL);
  CHECK(mco_get_storage_size(co) == MCO_DEFAULT_STORAGE_SIZE);
  CHECK(mco_push(co, &count, sizeof(count)) == MCO_SUCCESS);
  for (i = 0; i < 10; ++i) {
    CHECK(mco_resume(co) == MCO_SUCCESS);
    CHECK(mco_status(co) == MCO_SUSPENDED);
    CHECK(mco_get_bytes_stored(co) == sizeof(int));
    CHECK(mco_pop(co, &got[i], sizeof(got[i])) == MCO_SUCCESS);
  }
  CHECK(memcmp(got, expected, sizeof(got)) == 0);
  CHECK(mco_running() == NULL);
  /* The eleventh resume returns from the entry function. */
  CHECK(mco_resume(co) == MCO_SUCCESS);
  CHECK(mco_status(co) == MCO_DEAD);
  CHECK(mco_resume(co) == MCO_NOT_SUSPENDED);
  CHECK(mco_destroy(co) == MCO_SUCCESS);
}

static void inner_entry(mco_coro *inner)
{
  mco_coro *outer = NULL;
  CHECK(mco_pop(inner, &outer, sizeof(outer)) == MCO_SUCCESS);
  CHECK(mco_running() == inner);
  CHECK(mco_status(outer) == MCO_NORMAL);
  CHECK(mco_yield(mco_running()) == MCO_SUCCESS);
  CHECK(mco_status(outer) == MCO_NORMAL);
}

static void outer_entry(mco_coro *outer)
{
  mco_desc desc = mco_desc_init(inner_entry, 0);
  mco_coro *inner = NULL;
  CHECK(mco_create(&inner, &desc) == MCO_SUCCESS);
  if (inner == NULL)
    return;
  CHECK(mco_push(inner, &outer, sizeof(outer)) == MCO_SUCCESS);
  CHECK(mco_resume(inner) == MCO_SUCCESS);
  CHECK(mco_status(inner) == MCO_SUSPENDED);
  CHECK(mco_status(outer) == MCO_RUNNING);
  CHECK(mco_running() == outer);
  /* A running coroutine can be neither resumed nor destroyed. */
  CHECK(mco_resume(outer) == MCO_NOT_SUSPENDED);
  CHECK(mco_destroy(outer) == MCO_INVALID_OPERATION);
  CHECK(mco_resume(inner) == MCO_SUCCESS);
  CHECK(mco_status(inner) == MCO_DEAD);
  CHECK(mco_destroy(inner) == MCO_SUCCESS);
}

static void test_nested(void)
{
  mco_desc desc = mco_desc_init(outer_entry, 0);
  mco_coro *outer = NULL;
  CHECK(mco_create(&outer, &desc) == MCO_SUCCESS);
  if (outer == NULL)
    return;
  CHECK(mco_resume(outer) == MCO_SUCCESS);
  CHECK(mco_status(outer) == MCO_DEAD);
  CHECK(mco_destroy(outer) == MCO_SUCCESS);
}

struct counting_allocator {
  int allocations;
  int frees;
  size_t size;
};

static void *counting_malloc(size_t size, void *allocator_data)
{
  struct counting_allocator *counts = (struct counting_allocator *)allocator_data;
  ++counts->allocations;
  counts->size = size;
  return malloc(size);
}

static void counting_free(void *ptr, void *allocator_data)
{
  ++((struct counting_allocator *)allocator_data)->frees;
  free(ptr);
}

/* Fills 64 KiB of its stack, more than the 56 KiB default holds, and reads
 * it back after a yield: byte i holds i / 4096, so the bytes at multiples of
 * 4096 sum to 0 + 1 + ... + 15 = 120. */
static void deep_entry(mco_coro *co)
{
  volatile unsigned char big[64 * 1024];
  size_t i;
  unsigned sum = 0;
  for (i = 0; i < sizeof(big); ++i)
    big[i] = (unsigned char)(i / 4096);
  CHECK(mco_yield(co) == MCO_SUCCESS);
  for (i = 0; i < sizeof(big); i += 4096)
    sum += big[i];
  CHECK(mco_push(co, &sum, sizeof(sum)) == MCO_SUCCESS);
}

static void test_allocator_and_stack(void)
{
  struct counting_allocator counts = {0, 0, 0};
  mco_desc desc = mco_desc_init(deep_entry, 128 * 1024);
  mco_coro *co = NULL;
  unsigned sum = 0;
  desc.malloc_cb = counting_malloc;
  desc.free_cb = counting_free;
  desc.allocator_data = &counts;
  CHECK(mco_create(&co, &desc) == MCO_SUCCESS);
  if (co == NULL)
    return;
  CHECK(counts.allocations == 1 && counts.size == desc.coro_size);
  CHECK(co->stack_size == 128 * 1024);
  CHECK(mco_resume(co) == MCO_SUCCESS);
  CHECK(mco_status(co) == MCO_SUSPENDED);
  CHECK(mco_resume(co) == MCO_SUCCESS);
  CHECK(mco_status(co) == MCO_DEAD);
  CHECK(mco_pop(co, &sum, sizeof(sum)) == MCO_SUCCESS && sum == 120);
  CHECK(mco_destroy(co) == MCO_SUCCESS);
  CHECK(counts.allocations == 1 && counts.frees == 1);
}

static void count_entry(mco_coro *co)
{
  int *calls = (int *)mco_get_user_data(co);
  ++*calls;
  CHECK(mco_yield(co) == MCO_SUCCESS);
  ++*calls;
}

static void test_caller_allocated(void)
{
  mco_desc desc = mco_desc_init(count_entry, 0);
  void *memory = malloc(desc.coro_size);
  mco_coro *co = (mco_coro *)memory;
  int calls = 0;
  CHECK(memory != NULL);
  if (memory == NULL)
    return;
  desc.user_data = &calls;
  CHECK(mco_init(co, &desc) == MCO_SUCCESS);
  CHECK(mco_resume(co) == MCO_SUCCESS && calls == 1);
  CHECK(mco_resume(co) == MCO_SUCCESS && calls == 2);
  CHECK(mco_status(co) == MCO_DEAD);
  CHECK(mco_uninit(co) == MCO_SUCCESS);
  free(memory);
}

static void test_misuse(void)
{
  static mco_coro sentinel; /* failed creations reset the output to NULL */
  mco_desc desc = mco_desc_init(count_entry, 0);
  mco_coro *co = &sentinel;
  unsigned char bytes[17];
  int calls = 0;
  int first;
  int second;
  int out = 0;

  CHECK(mco_create(NULL, &desc) == MCO_INVALID_POINTER);
  desc.func = NULL;
  CHECK(mco_create(&co, &desc) == MCO_INVALID_ARGUMENTS && co == NULL);
  /* mco_desc_init raises a small stack size; an edited one is rejected. */
  desc = mco_desc_init(count_entry, 16);
  CHECK(desc.stack_size == MCO_MIN_STACK_SIZE);
  desc.stack_size = MCO_MIN_STACK_SIZE - 16;
  co = &sentinel;
  CHECK(mco_create(&co, &desc) == MCO_INVALID_ARGUMENTS && co == NULL);

  CHECK(mco_resume(NULL) == MCO_INVALID_COROUTINE);
  CHECK(mco_yield(NULL) == MCO_INVALID_COROUTINE);
  CHECK(mco_destroy(NULL) == MCO_INVALID_COROUTINE);
  CHECK(mco_status(NULL) == MCO_DEAD);
  CHECK(mco_get_bytes_stored(NULL) == 0);
  CHECK(strcmp(mco_result_description(MCO_NOT_ENOUGH_SPACE), "Not enough space") == 0);

  /* A 16-byte storage. */
  desc = mco_desc_init(count_entry, 0);
  desc.user_data = &calls;
  desc.storage_size = 16;
  CHECK(mco_create(&co, &desc) == MCO_SUCCESS);
  if (co == NULL)
    return;
  memset(bytes, 7, sizeof(bytes));
  CHECK(mco_get_storage_size(co) == 16);
  CHECK(mco_push(co, bytes, 17) == MCO_NOT_ENOUGH_SPACE);
  CHECK(mco_push(co, NULL, 1) == MCO_INVALID_POINTER);
  CHECK(mco_push(co, bytes, 16) == MCO_SUCCESS);
  CHECK(mco_push(co, bytes, 1) == MCO_NOT_ENOUGH_SPACE);
  CHECK(mco_peek(co, NULL, 1) == MCO_INVALID_POINTER);
  CHECK(mco_pop(co, bytes, 17) == MCO_NOT_ENOUGH_SPACE);
  CHECK(mco_pop(co, NULL, 16) == MCO_SUCCESS && mco_get_bytes_stored(co) == 0);
  CHECK(mco_pop(co, bytes, 1) == MCO_NOT_ENOUGH_SPACE);

  /* The storage is LIFO, and peeking does not consume. */
  first = 1;
  second = 2;
  CHECK(mco_push(co, &first, sizeof(first)) == MCO_SUCCESS);
  CHECK(mco_push(co, &second, sizeof(second)) == MCO_SUCCESS);
  CHECK(mco_peek(co, &out, sizeof(out)) == MCO_SUCCESS && out == 2);
  CHECK(mco_get_bytes_stored(co) == 2 * sizeof(int));
  CHECK(mco_pop(co, &out, sizeof(out)) == MCO_SUCCESS && out == 2);
  CHECK(mco_pop(co, &out, sizeof(out)) == MCO_SUCCESS && out == 1);

  /* A coroutine suspended mid-body can be destroyed. */
  CHECK(mco_resume(co) == MCO_SUCCESS && calls == 1);
  CHECK(mco_status(co) == MCO_SUSPENDED);
  CHECK(mco_destroy(co) == MCO_SUCCESS);
}

int main(void)
{
  test_generator();
  test_nested();
  test_allocator_and_stack();
  test_caller_allocated();
  test_misuse();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("minicoro 0.1.3 ok");
  return 0;
}
