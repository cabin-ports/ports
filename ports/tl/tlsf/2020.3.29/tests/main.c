/* tlsf in C: pools, malloc/memalign/realloc/free, block accounting and the
   internal consistency checks, against exact expected values. */
#include <stdio.h>
#include <string.h>

#include <tlsf.h>

static int failures = 0;

#define CHECK(expr)                                                      \
  do {                                                                   \
    if (!(expr)) {                                                       \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #expr); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

#define ARENA (1u << 20)
static union {
  void* p;
  double d;
  long long ll;
  unsigned char bytes[ARENA];
} arena;

typedef struct {
  size_t used, free_blocks, used_bytes;
} stats;

static void walker(void* ptr, size_t size, int used, void* user) {
  stats* s = (stats*)user;
  (void)ptr;
  if (used) {
    ++s->used;
    s->used_bytes += size;
  } else {
    ++s->free_blocks;
  }
}

static stats walk(pool_t pool) {
  stats s = {0, 0, 0};
  tlsf_walk_pool(pool, walker, &s);
  return s;
}

static size_t align_up(size_t n, size_t a) { return (n + a - 1) & ~(a - 1); }

static void fill(void* p, size_t n, unsigned char v) { memset(p, v, n); }

static int filled(const void* p, size_t n, unsigned char v) {
  const unsigned char* b = (const unsigned char*)p;
  size_t i;
  for (i = 0; i < n; ++i)
    if (b[i] != v) return 0;
  return 1;
}

static void test_constants(void) {
  const size_t word = sizeof(size_t);
  const size_t align = word == 8 ? 8 : 4;
  CHECK(tlsf_align_size() == align);
  CHECK(tlsf_alloc_overhead() == word);
  CHECK(tlsf_pool_overhead() == 2 * word);
  CHECK(tlsf_block_size_min() == 3 * sizeof(void*));
  CHECK(tlsf_block_size_max() == ((size_t)1 << (word == 8 ? 32 : 30)));
  CHECK(tlsf_size() % align == 0);
}

static void test_malloc_free(void) {
  tlsf_t t = tlsf_create_with_pool(&arena, ARENA);
  pool_t pool = tlsf_get_pool(t);
  const size_t a = tlsf_align_size();
  void* p[64];
  size_t i;
  stats s;

  CHECK(t == (tlsf_t)&arena);
  CHECK(pool == (pool_t)((char*)&arena + tlsf_size()));
  s = walk(pool);
  CHECK(s.used == 0 && s.free_blocks == 1);

  CHECK(tlsf_malloc(t, 0) == NULL);
  p[0] = tlsf_malloc(t, 1);
  CHECK(p[0] != NULL);
  CHECK(tlsf_block_size(p[0]) == tlsf_block_size_min());
  tlsf_free(t, p[0]);

  p[0] = tlsf_malloc(t, 100);
  CHECK(tlsf_block_size(p[0]) == align_up(100, a));
  CHECK(((size_t)p[0] % a) == 0);
  tlsf_free(t, p[0]);
  tlsf_free(t, NULL);

  for (i = 0; i < 64; ++i) {
    p[i] = tlsf_malloc(t, 1000 + i);
    CHECK(p[i] != NULL);
    fill(p[i], 1000 + i, (unsigned char)(i + 1));
  }
  s = walk(pool);
  CHECK(s.used == 64 && s.free_blocks == 1);
  for (i = 0; i < 64; ++i) CHECK(filled(p[i], 1000 + i, (unsigned char)(i + 1)));
  CHECK(tlsf_check(t) == 0);
  CHECK(tlsf_check_pool(pool) == 0);

  /* Free every other block, then the rest: neighbours must coalesce. */
  for (i = 0; i < 64; i += 2) tlsf_free(t, p[i]);
  s = walk(pool);
  CHECK(s.used == 32 && s.free_blocks == 33);
  for (i = 1; i < 64; i += 2) tlsf_free(t, p[i]);
  s = walk(pool);
  CHECK(s.used == 0 && s.free_blocks == 1);
  CHECK(tlsf_check(t) == 0);

  p[0] = tlsf_malloc(t, ARENA / 2);
  CHECK(p[0] != NULL);
  CHECK(tlsf_malloc(t, ARENA) == NULL);
  tlsf_free(t, p[0]);
  tlsf_destroy(t);
}

static void test_memalign(void) {
  tlsf_t t = tlsf_create_with_pool(&arena, ARENA);
  pool_t pool = tlsf_get_pool(t);
  static const size_t aligns[] = {16, 64, 256, 4096};
  void* p[4];
  size_t i;
  stats s;

  for (i = 0; i < 4; ++i) {
    p[i] = tlsf_memalign(t, aligns[i], 300);
    CHECK(p[i] != NULL);
    CHECK(((size_t)p[i] % aligns[i]) == 0);
    CHECK(tlsf_block_size(p[i]) >= 300);
    fill(p[i], 300, (unsigned char)(0xa0 + i));
  }
  for (i = 0; i < 4; ++i) CHECK(filled(p[i], 300, (unsigned char)(0xa0 + i)));
  CHECK(tlsf_check(t) == 0);
  for (i = 0; i < 4; ++i) tlsf_free(t, p[i]);
  s = walk(pool);
  CHECK(s.used == 0 && s.free_blocks == 1);
}

static void test_realloc(void) {
  tlsf_t t = tlsf_create_with_pool(&arena, ARENA);
  pool_t pool = tlsf_get_pool(t);
  char* p = (char*)tlsf_realloc(t, NULL, 64);
  char* q;
  char* guard;
  stats s;

  CHECK(p != NULL);
  fill(p, 64, 0x5a);

  /* The block after p is free, so growing and shrinking stay in place. */
  q = (char*)tlsf_realloc(t, p, 4096);
  CHECK(q == p);
  CHECK(tlsf_block_size(q) >= 4096);
  CHECK(filled(q, 64, 0x5a));
  q = (char*)tlsf_realloc(t, p, 32);
  CHECK(q == p);
  CHECK(filled(q, 32, 0x5a));

  /* With a live neighbour the block moves and keeps its contents. */
  guard = (char*)tlsf_malloc(t, 64);
  CHECK(guard != NULL);
  q = (char*)tlsf_realloc(t, p, 8192);
  CHECK(q != NULL && q != p);
  CHECK(filled(q, 32, 0x5a));

  /* A request the pool cannot satisfy leaves the block untouched. */
  CHECK(tlsf_realloc(t, q, (size_t)ARENA * 4) == NULL);
  CHECK(filled(q, 32, 0x5a));
  CHECK(tlsf_block_size(q) >= 8192);

  CHECK(tlsf_realloc(t, q, 0) == NULL);
  tlsf_free(t, guard);
  s = walk(pool);
  CHECK(s.used == 0 && s.free_blocks == 1);
  CHECK(tlsf_check(t) == 0);
}

static void test_pools(void) {
  static union {
    void* p;
    unsigned char bytes[1 << 16];
  } a, b;
  static union {
    void* p;
    unsigned char bytes[1 << 14];
  } control;
  tlsf_t t = tlsf_create(&control);
  pool_t pa, pb;
  void* x;
  void* y;

  CHECK(t != NULL);
  CHECK(tlsf_size() <= sizeof(control));
  CHECK(tlsf_malloc(t, 16) == NULL);
  pa = tlsf_add_pool(t, a.bytes, sizeof(a));
  pb = tlsf_add_pool(t, b.bytes, sizeof(b));
  CHECK(pa == (pool_t)a.bytes && pb == (pool_t)b.bytes);

  x = tlsf_malloc(t, 40000);
  y = tlsf_malloc(t, 40000);
  CHECK(x != NULL && y != NULL);
  CHECK(walk(pa).used == 1 && walk(pb).used == 1);
  CHECK(tlsf_check(t) == 0);
  CHECK(tlsf_check_pool(pa) == 0 && tlsf_check_pool(pb) == 0);
  tlsf_free(t, x);
  tlsf_free(t, y);

  tlsf_remove_pool(t, pb);
  x = tlsf_malloc(t, 40000);
  CHECK(x != NULL);
  CHECK(walk(pa).used == 1);
  CHECK(tlsf_malloc(t, 40000) == NULL);
  CHECK(tlsf_check(t) == 0);
}

int main(void) {
  test_constants();
  test_malloc_free();
  test_memalign();
  test_realloc();
  test_pools();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("tlsf: ok");
  return 0;
}
