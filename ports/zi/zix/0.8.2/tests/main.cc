// zix from C++ (its declarations, but bump_allocator.h's, are extern "C"):
// a B-tree of 100 keys
// searched with lower_bound, and relative paths.
#include <zix/zix.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  define SEP "\\"
#else
#  define SEP "/"
#endif

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

int compare_ints(const void* a, const void* b, const void*) {
  const auto x = reinterpret_cast<std::intptr_t>(a);
  const auto y = reinterpret_cast<std::intptr_t>(b);
  return (x > y) - (x < y);
}

void* key(std::intptr_t value) { return reinterpret_cast<void*>(value); }

std::intptr_t value(ZixBTreeIter iter) {
  return reinterpret_cast<std::intptr_t>(zix_btree_get(iter));
}

}  // namespace

int main() {
  // ZIX_STATIC_STRING's C++ form is a braced initializer.
  const ZixStringView name = ZIX_STATIC_STRING("zix");
  CHECK(name.length == 3U && zix_string_view_equals(name, zix_string("zix")));

  // B-tree: the even numbers 0..198, inserted out of order.
  ZixBTree* const tree = zix_btree_new(nullptr, compare_ints, nullptr);
  CHECK(tree != nullptr);
  for (std::intptr_t i = 0; i < 100; ++i) {
    CHECK(zix_btree_insert(tree, key((i * 37 % 100) * 2)) ==
          ZIX_STATUS_SUCCESS);
  }
  CHECK(zix_btree_insert(tree, key(42)) == ZIX_STATUS_EXISTS);
  CHECK(zix_btree_size(tree) == 100U);

  std::intptr_t expected = 0;
  for (ZixBTreeIter i = zix_btree_begin(tree); !zix_btree_iter_is_end(i);
       zix_btree_iter_increment(&i)) {
    CHECK(value(i) == expected);
    expected += 2;
  }
  CHECK(expected == 200);

  ZixBTreeIter iter = zix_btree_end_iter;
  CHECK(zix_btree_find(tree, key(64), &iter) == ZIX_STATUS_SUCCESS);
  CHECK(value(iter) == 64);
  CHECK(zix_btree_find(tree, key(65), &iter) == ZIX_STATUS_NOT_FOUND);
  CHECK(zix_btree_lower_bound(tree, compare_ints, nullptr, key(65), &iter) ==
        ZIX_STATUS_SUCCESS);
  CHECK(value(iter) == 66);
  CHECK(zix_btree_lower_bound(tree, compare_ints, nullptr, key(500), &iter) ==
            ZIX_STATUS_SUCCESS &&
        zix_btree_iter_is_end(iter));

  void* removed = nullptr;
  ZixBTreeIter next = zix_btree_end_iter;
  CHECK(zix_btree_remove(tree, key(64), &removed, &next) ==
        ZIX_STATUS_SUCCESS);
  CHECK(removed == key(64) && value(next) == 66);
  CHECK(zix_btree_size(tree) == 99U);
  zix_btree_free(tree, nullptr, nullptr);

  // Paths relative to a base, and back.
  char* const relative = zix_path_lexically_relative(nullptr, "a/b/c.txt", "a/d");
  // The suffix after the up-references is copied as written.
  CHECK(relative && std::strcmp(relative, ".." SEP "b/c.txt") == 0);
  char* const joined = zix_path_join(nullptr, "a/d", relative);
  char* const normal = zix_path_lexically_normal(nullptr, joined);
  CHECK(normal && std::strcmp(normal, "a" SEP "b" SEP "c.txt") == 0);
  zix_free(nullptr, normal);
  zix_free(nullptr, joined);
  zix_free(nullptr, relative);

  if (failures != 0) {
    return 1;
  }
  std::printf("zix ok\n");
  return 0;
}
