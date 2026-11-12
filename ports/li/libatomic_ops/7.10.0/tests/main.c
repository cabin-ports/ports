/* Drives libatomic_ops from C89: the core's loads, stores, fetch-and-add,
   compare-and-swap and test-and-set on AO_t, then atomic_ops_gpl's stack,
   which must pop in LIFO order, and its malloc, whose blocks must not
   overlap and whose large objects come from mmap where the build has it.
   The headers' alignment assertions stay on, which a release build drops. */
#undef NDEBUG

#include <atomic_ops.h>
#include <atomic_ops_malloc.h>
#include <atomic_ops_stack.h>

#include <stdio.h>
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

struct node {
  AO_uintptr_t next; /* the link AO_stack_push takes comes first */
  int value;
};

static volatile AO_t word;
static AO_TS_t flag = AO_TS_INITIALIZER;
static AO_stack_t stack = AO_STACK_INITIALIZER;

static void core(void)
{
  AO_store(&word, 5);
  CHECK(AO_load(&word) == 5);
  CHECK(AO_fetch_and_add1(&word) == 5);
  CHECK(AO_fetch_and_add(&word, 10) == 6);
  CHECK(AO_fetch_and_sub1(&word) == 16);
  CHECK(AO_compare_and_swap(&word, 15, 20));
  CHECK(!AO_compare_and_swap(&word, 15, 30));
  CHECK(AO_fetch_compare_and_swap(&word, 20, 7) == 20);
  CHECK(AO_fetch_compare_and_swap(&word, 20, 9) == 7);
  CHECK(AO_load(&word) == 7);

  CHECK(AO_test_and_set_full(&flag) == AO_TS_CLEAR);
  CHECK(AO_test_and_set_full(&flag) == AO_TS_SET);
  AO_CLEAR(&flag);
  CHECK(AO_test_and_set_full(&flag) == AO_TS_CLEAR);
}

static void lifo(void)
{
  AO_uintptr_t *link;
  struct node *n;
  int i;

  for (i = 1; i <= 5; ++i) {
    n = (struct node *)AO_malloc(sizeof(struct node));
    CHECK(n != NULL);
    if (n == NULL)
      return;
    n->value = i;
    AO_stack_push(&stack, &n->next);
  }
  for (i = 5; i >= 1; --i) {
    link = AO_stack_pop(&stack);
    CHECK(link != NULL);
    if (link == NULL)
      return;
    CHECK(((struct node *)link)->value == i);
    AO_free(link);
  }
  CHECK(AO_stack_pop(&stack) == NULL);
}

static void heap(void)
{
  /* Below the 32 to 64 KiB requests AO_malloc cannot serve (cabin.toml). */
  static const size_t sizes[6] = {1, 24, 100, 1000, 4000, 30000};
  char *blocks[6];
  char *big;
  size_t big_size = (size_t)1 << 20;
  int i;

  for (i = 0; i < 6; ++i) {
    blocks[i] = (char *)AO_malloc(sizes[i]);
    CHECK(blocks[i] != NULL);
    if (blocks[i] == NULL)
      return;
    memset(blocks[i], 'a' + i, sizes[i]);
  }
  for (i = 0; i < 6; ++i) {
    CHECK(blocks[i][0] == 'a' + i && blocks[i][sizes[i] - 1] == 'a' + i);
    AO_free(blocks[i]);
  }

  /* Larger objects than a 64 KiB chunk holds need mmap, which this call
     turns on; the Windows build has none. */
  AO_malloc_enable_mmap();
  big = (char *)AO_malloc(big_size);
#ifdef _WIN32
  CHECK(big == NULL);
#else
  CHECK(big != NULL);
  if (big != NULL) {
    big[0] = big[big_size - 1] = 'z';
    CHECK(big[0] == 'z' && big[big_size - 1] == 'z');
    AO_free(big);
  }
#endif
}

int main(void)
{
  CHECK(AO_VERSION_MAJOR == 7 && AO_VERSION_MINOR == 10 && AO_VERSION_MICRO == 0);
  core();
  lifo();
  heap();
  printf("libatomic_ops C: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
