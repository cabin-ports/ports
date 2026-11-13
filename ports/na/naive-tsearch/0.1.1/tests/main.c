/* naive-tsearch: the POSIX tsearch family from this package, not the C
 * library's.  <search.h> must be the package's header, and a tree built in
 * ascending order must keep the unbalanced shape of upstream's binary search
 * tree, which glibc's balanced tsearch does not give.  macOS's libc has no
 * tdestroy and the Windows UCRT no tsearch, so there the program links only
 * against this package. */
#include <search.h>
#ifndef NAIVE_TSEARCH_SEARCH_H_
#error "<search.h> is not naive-tsearch's"
#endif

#include <stdio.h>

static int failures = 0;

#define CHECK(expr)                                                       \
  do {                                                                    \
    if (!(expr)) {                                                        \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #expr); \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

static int compare_ints(const void *a, const void *b) {
  int x = *(const int *)a;
  int y = *(const int *)b;
  return (x > y) - (x < y);
}

/* A node's first member is its key pointer (POSIX). */
static int node_key(const void *node) { return **(int *const *)node; }

static int order[16];
static int order_len;
static int depth_of[16];
static int leaves;

static void record(const void *node, VISIT which, int depth) {
  int key = node_key(node);
  if (which == preorder || which == leaf) {
    depth_of[key] = depth;
  }
  if (which == postorder || which == leaf) {
    order[order_len++] = key;
  }
  if (which == leaf) {
    ++leaves;
  }
}

static void walk(const void *root) {
  order_len = 0;
  leaves = 0;
  twalk(root, record);
}

static int freed = 0;

static void count_free(void *key) {
  (void)key;
  ++freed;
}

int main(void) {
  static int keys[] = {5, 3, 8, 1};
  static int more[] = {7, 6};
  static int chain[] = {1, 2, 3, 4, 5, 6, 7};
  int dup3 = 3;
  int seven = 7;
  void *root = NULL;
  void *node;
  int i;

  for (i = 0; i < 4; ++i) {
    node = tsearch(&keys[i], &root, compare_ints);
    CHECK(node != NULL && *(int **)node == &keys[i]);
  }
  /* An equal key returns the existing node and stores nothing. */
  node = tsearch(&dup3, &root, compare_ints);
  CHECK(node != NULL && *(int **)node == &keys[1]);

  node = tfind(&dup3, &root, compare_ints);
  CHECK(node != NULL && *(int **)node == &keys[1]);
  CHECK(tfind(&seven, &root, compare_ints) == NULL);

  /* postorder and leaf visits come in key order. */
  walk(root);
  CHECK(order_len == 4 && order[0] == 1 && order[1] == 3 && order[2] == 5 &&
        order[3] == 8);
  CHECK(depth_of[5] == 0 && depth_of[3] == 1 && depth_of[8] == 1 &&
        depth_of[1] == 2);

  /* tdelete returns a node it has freed, so only a miss's NULL is checked. */
  tdelete(&dup3, &root, compare_ints);
  CHECK(tfind(&dup3, &root, compare_ints) == NULL);
  CHECK(tdelete(&seven, &root, compare_ints) == NULL);
  walk(root);
  CHECK(order_len == 3 && order[0] == 1 && order[1] == 5 && order[2] == 8);

  /* 5's right child 8 has a left subtree, so 5's in-order successor 6 takes
   * its place. */
  for (i = 0; i < 2; ++i) {
    tsearch(&more[i], &root, compare_ints);
  }
  tdelete(&keys[0], &root, compare_ints);
  CHECK(tfind(&keys[0], &root, compare_ints) == NULL);
  walk(root);
  CHECK(order_len == 4 && order[0] == 1 && order[1] == 6 && order[2] == 7 &&
        order[3] == 8);
  CHECK(depth_of[6] == 0 && depth_of[1] == 1 && depth_of[8] == 1 &&
        depth_of[7] == 2);

  tdestroy(root, count_free);
  CHECK(freed == 4);

  /* Ascending inserts make a right-leaning chain: key k at depth k - 1. */
  root = NULL;
  for (i = 0; i < 7; ++i) {
    tsearch(&chain[i], &root, compare_ints);
  }
  walk(root);
  CHECK(order_len == 7 && leaves == 1 && order[6] == 7);
  for (i = 1; i <= 7; ++i) {
    CHECK(depth_of[i] == i - 1);
  }

  /* Deleting the root until the tree is empty removes every node. */
  for (i = 0; root != NULL && i < 8; ++i) {
    tdelete(*(int **)root, &root, compare_ints);
  }
  CHECK(root == NULL && i == 7);

  if (failures != 0) {
    return 1;
  }
  printf("naive-tsearch ok (C)\n");
  return 0;
}
