// zix from C: string views, paths (one in a bump allocator's buffer), an
// ordered tree, a ring buffer, the digest, a file copied in a temporary
// directory, and a thread that wakes the main thread through a semaphore.
#include <zix/zix.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  define SEP "\\"
#else
#  define SEP "/"
#endif

static int failures = 0;

#define CHECK(expr)                                                     \
  do {                                                                  \
    if (!(expr)) {                                                      \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #expr); \
      ++failures;                                                       \
    }                                                                   \
  } while (0)

static int
compare_ints(const void* a, const void* b, const void* user_data)
{
  (void)user_data;
  const intptr_t x = (intptr_t)a;
  const intptr_t y = (intptr_t)b;
  return (x > y) - (x < y);
}

typedef struct {
  ZixSem sem;
  int    value;
} Shared;

static ZixThreadResult ZIX_THREAD_FUNC
worker(void* arg)
{
  Shared* const shared = (Shared*)arg;
  shared->value        = 42;
  zix_sem_post(&shared->sem);
  return ZIX_THREAD_RESULT;
}

static int
view_is(ZixStringView view, const char* expected)
{
  return zix_string_view_equals(view, zix_string(expected));
}

int
main(void)
{
  // String views
  const ZixStringView hello = zix_string("hello, world");
  CHECK(hello.length == 12U);
  CHECK(zix_string_view_equals(zix_substring(hello.data, 5U),
                               zix_string("hello")));
  CHECK(!zix_string_view_equals(hello, zix_string("hello")));

  // Paths: joining, lexical normalization and decomposition
  char* const joined = zix_path_join(NULL, "dir", "file.txt");
  CHECK(joined && !strcmp(joined, "dir" SEP "file.txt"));
  zix_free(NULL, joined);

  char* const normal = zix_path_lexically_normal(NULL, "a/./b/../c//d.tar.gz");
  CHECK(normal && !strcmp(normal, "a" SEP "c" SEP "d.tar.gz"));
  zix_free(NULL, normal);

  // A path built in a caller-owned buffer by the bump allocator
  uintmax_t        buffer[32];
  char* const      bytes = (char*)buffer;
  ZixBumpAllocator bump  = zix_bump_allocator(sizeof(buffer), buffer);
  char* const      usr   = zix_path_join(&bump.base, "usr", "lib");
  CHECK(usr && !strcmp(usr, "usr" SEP "lib"));
  CHECK(usr >= bytes && usr < bytes + sizeof(buffer));
  zix_free(&bump.base, usr);

  CHECK(view_is(zix_path_filename("lib/archive.tar.gz"), "archive.tar.gz"));
  CHECK(view_is(zix_path_stem("lib/archive.tar.gz"), "archive.tar"));
  CHECK(view_is(zix_path_extension("lib/archive.tar.gz"), ".gz"));
  CHECK(view_is(zix_path_parent_path("lib/archive.tar.gz"), "lib"));
  CHECK(zix_path_is_relative("lib/archive.tar.gz"));

  // Tree: unique keys, kept in order
  ZixTree* const tree = zix_tree_new(NULL, false, compare_ints, NULL, NULL, NULL);
  CHECK(tree != NULL);
  static const intptr_t keys[] = {5, 1, 4, 2, 3};
  for (size_t i = 0U; i < sizeof(keys) / sizeof(keys[0]); ++i) {
    CHECK(zix_tree_insert(tree, (void*)keys[i], NULL) == ZIX_STATUS_SUCCESS);
  }
  CHECK(zix_tree_insert(tree, (void*)(intptr_t)3, NULL) == ZIX_STATUS_EXISTS);
  CHECK(zix_tree_size(tree) == 5U);
  intptr_t expected = 1;
  for (ZixTreeIter* i = zix_tree_begin(tree); !zix_tree_iter_is_end(i);
       i               = zix_tree_iter_next(i)) {
    CHECK((intptr_t)zix_tree_get(i) == expected);
    ++expected;
  }
  CHECK(expected == 6);
  ZixTreeIter* found = NULL;
  CHECK(zix_tree_find(tree, (void*)(intptr_t)4, &found) == ZIX_STATUS_SUCCESS);
  CHECK(found && (intptr_t)zix_tree_get(found) == 4);
  CHECK(zix_tree_find(tree, (void*)(intptr_t)9, &found) ==
        ZIX_STATUS_NOT_FOUND);
  zix_tree_free(tree);

  // Ring buffer
  ZixRing* const ring = zix_ring_new(NULL, 16U);
  CHECK(ring != NULL);
  CHECK(zix_ring_write(ring, "abcde", 5U) == 5U);
  CHECK(zix_ring_read_space(ring) == 5U);
  char out[8] = {0};
  CHECK(zix_ring_peek(ring, out, 2U) == 2U && !memcmp(out, "ab", 2U));
  CHECK(zix_ring_read(ring, out, 5U) == 5U && !memcmp(out, "abcde", 5U));
  CHECK(zix_ring_read_space(ring) == 0U);
  zix_ring_free(ring);

  // Digest: the aligned variant equals the general one on aligned data
  static const uint64_t words64[4] = {1U, 2U, 3U, 4U};
  static const uint32_t words32[4] = {1U, 2U, 3U, 4U};
  CHECK(zix_digest64(7U, words64, sizeof(words64)) ==
        zix_digest64_aligned(7U, words64, sizeof(words64)));
  CHECK(zix_digest32(7U, words32, sizeof(words32)) ==
        zix_digest32_aligned(7U, words32, sizeof(words32)));
  CHECK(zix_digest32(7U, "abc", 3U) != zix_digest32(7U, "abd", 3U));

  // Filesystem: create, copy, compare and remove in a temporary directory
  char* const temp    = zix_temp_directory_path(NULL);
  char* const pattern = zix_path_join(NULL, temp, "zixtestXXXXXX");
  char* const dir     = zix_create_temporary_directory(NULL, pattern);
  CHECK(dir != NULL);
  if (dir) {
    CHECK(zix_file_type(dir) == ZIX_FILE_TYPE_DIRECTORY);
    char* const src = zix_path_join(NULL, dir, "src.txt");
    char* const dst = zix_path_join(NULL, dir, "dst.txt");

    static const char text[] = "zix copies this file\n";
    FILE* const       file   = fopen(src, "wb");
    CHECK(file != NULL);
    if (file) {
      CHECK(fwrite(text, 1U, sizeof(text) - 1U, file) == sizeof(text) - 1U);
      fclose(file);
    }

    CHECK(zix_file_type(src) == ZIX_FILE_TYPE_REGULAR);
    CHECK(zix_file_size(src) == (ZixFileOffset)(sizeof(text) - 1U));
    CHECK(zix_copy_file(NULL, src, dst, 0U) == ZIX_STATUS_SUCCESS);
    CHECK(zix_file_equals(NULL, src, dst));
    CHECK(zix_copy_file(NULL, src, dst, 0U) == ZIX_STATUS_EXISTS);

    CHECK(zix_remove(dst) == ZIX_STATUS_SUCCESS);
    CHECK(zix_remove(src) == ZIX_STATUS_SUCCESS);
    CHECK(zix_remove(dir) == ZIX_STATUS_SUCCESS);
    CHECK(zix_file_type(dir) == ZIX_FILE_TYPE_NONE);
    zix_free(NULL, dst);
    zix_free(NULL, src);
  }
  zix_free(NULL, dir);
  zix_free(NULL, pattern);
  zix_free(NULL, temp);

  // Threads and semaphores
  Shared shared;
  shared.value = 0;
  // Without a semaphore or a worker to post it, the wait would block.
  if (zix_sem_init(&shared.sem, 0U) != ZIX_STATUS_SUCCESS) {
    fprintf(stderr, "main.c: zix_sem_init failed\n");
    return 1;
  }
  CHECK(zix_sem_try_wait(&shared.sem) != ZIX_STATUS_SUCCESS);
  ZixThread thread;
  if (zix_thread_create(&thread, 128U * 1024U, worker, &shared) !=
      ZIX_STATUS_SUCCESS) {
    fprintf(stderr, "main.c: zix_thread_create failed\n");
    return 1;
  }
  CHECK(zix_sem_wait(&shared.sem) == ZIX_STATUS_SUCCESS);
  CHECK(shared.value == 42);
  CHECK(zix_thread_join(thread) == ZIX_STATUS_SUCCESS);
  CHECK(zix_sem_destroy(&shared.sem) == ZIX_STATUS_SUCCESS);

  if (failures) {
    return 1;
  }
  printf("zix ok\n");
  return 0;
}
