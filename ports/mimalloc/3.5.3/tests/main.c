/* The C consumer: mimalloc's allocation API (zeroing, reallocation, usable
   sizes, alignment, strings and the POSIX-style error reporting), a heap of
   its own whose blocks are visited and then destroyed at once, statistics
   through mimalloc-stats.h, the system malloc left alone, since this build
   does not override it, and the explicit route instead: mimalloc-override.h
   redirecting the standard names in the code that includes it. */
#include <mimalloc.h>
#include <mimalloc-stats.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
              #cond);                                                  \
      exit(1);                                                         \
    }                                                                  \
  } while (0)

/* Defined at the end, after mimalloc-override.h. */
static void *overridden_malloc(size_t size);
static void overridden_free(void *p);

typedef struct {
  size_t blocks;
  size_t bytes;
} tally_t;

/* Counts the live blocks; the visitor is also called once per area with a
   NULL block. */
static bool mi_cdecl tally(const mi_heap_t *heap, const mi_heap_area_t *area,
                           void *block, size_t block_size, void *arg) {
  (void)heap;
  (void)area;
  if (block != NULL) {
    tally_t *t = (tally_t *)arg;
    ++t->blocks;
    t->bytes += block_size;
  }
  return true;
}

int main(void) {
  CHECK(mi_version() == MI_MALLOC_VERSION && MI_MALLOC_VERSION == 30503);

  /* calloc zeroes, realloc keeps the contents, and the usable size covers
     the request; the block comes from the main heap. */
  unsigned char *p = (unsigned char *)mi_calloc(100, 1);
  CHECK(p != NULL && mi_usable_size(p) >= 100);
  for (int i = 0; i < 100; ++i) CHECK(p[i] == 0);
  memset(p, 0xab, 100);
  p = (unsigned char *)mi_realloc(p, 100000);
  CHECK(p != NULL && mi_usable_size(p) >= 100000);
  for (int i = 0; i < 100; ++i) CHECK(p[i] == 0xab);
  CHECK(mi_is_in_heap_region(p) && mi_heap_contains(mi_heap_main(), p));

  void *aligned = mi_malloc_aligned(1000, 256);
  CHECK(aligned != NULL && (uintptr_t)aligned % 256 == 0);
  mi_free(aligned);

  /* posix_memalign rejects an alignment that is not a power of two and
     leaves the output alone; reallocarray reports an overflowing count
     (which a debug build also prints to stderr). */
  void *q = &q;
  CHECK(mi_posix_memalign(&q, 24, 16) == EINVAL && q == &q);
  CHECK(mi_posix_memalign(&q, 64, 16) == 0 && (uintptr_t)q % 64 == 0);
  mi_free(q);
  errno = 0;
  CHECK(mi_reallocarray(NULL, SIZE_MAX / 2, 4) == NULL && errno == EOVERFLOW);

  char *s = mi_strndup("allocator", 5);
  CHECK(s != NULL && strcmp(s, "alloc") == 0);
  mi_free(s);

  /* A heap of its own: its blocks belong to it, not to the main heap, a
     visit sees each of them, and destroying the heap frees them all. */
  mi_heap_t *heap = mi_heap_new();
  CHECK(heap != NULL && heap != mi_heap_main());
  for (int i = 0; i < 10; ++i) {
    void *block = mi_heap_malloc(heap, 48);
    CHECK(block != NULL && mi_heap_contains(heap, block));
    CHECK(!mi_heap_contains(mi_heap_main(), block));
  }
  CHECK(!mi_heap_contains(heap, p));
  tally_t t = {0, 0};
  CHECK(mi_heap_visit_blocks(heap, true, tally, &t));
  CHECK(t.blocks == 10 && t.bytes >= 10 * 48);
  mi_heap_destroy(heap);
  mi_free(p);

  /* MI_OVERRIDE is off: malloc is still the C library's. */
  void *sys = malloc(64);
  CHECK(sys != NULL && !mi_is_in_heap_region(sys));
  free(sys);
  void *mine = overridden_malloc(64);
  CHECK(mine != NULL && mi_is_in_heap_region(mine));
  overridden_free(mine);

  /* The process statistics count the memory mimalloc reserved from the
     operating system. */
  mi_stats_t stats;
  mi_stats_init(&stats);
  CHECK(mi_stats_get(&stats));
  CHECK(stats.version == MI_STAT_VERSION);
  CHECK(stats.reserved.peak > 0 && stats.mmap_calls.total > 0);

  puts("ok");
  return 0;
}

#include <mimalloc-override.h>

static void *overridden_malloc(size_t size) { return malloc(size); }
static void overridden_free(void *p) { free(p); }
