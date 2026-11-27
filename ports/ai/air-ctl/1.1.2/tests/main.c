#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "CTL/algorithm/heap_sort.h"
#include "CTL/algorithm/insertion_sort.h"
#include "CTL/algorithm/intro_sort.h"
#include "CTL/algorithm/merge_sort.h"
#include "CTL/algorithm/quick_sort.h"
#include "CTL/algorithm/tim_sort.h"
#include "CTL/deque.h"
#include "CTL/hash_map.h"
#include "CTL/list.h"
#include "CTL/lockfree/mpmc_queue.h"
#include "CTL/lockfree/spsc_queue.h"
#if UINTPTR_MAX > 0xffffffffu /* the lockfree queue and stack need 64-bit pointers */
#include "CTL/lockfree/queue.h"
#include "CTL/lockfree/stack.h"
#endif
#include "CTL/priority_queue.h"
#include "CTL/queue.h"
#include "CTL/stack.h"
#include "CTL/tree_map.h"
#include "CTL/vector.h"

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                  \
    }                                                            \
  } while (0)

static bool less_int(const void *a, const void *b) {
  return *(const int *)a < *(const int *)b;
}

static bool greater_int(const void *a, const void *b) {
  return *(const int *)a > *(const int *)b;
}

static bool equal_int(const void *a, const void *b) {
  return *(const int *)a == *(const int *)b;
}

static int test_vector(void) {
  CTL_vector v;
  CTL_vector_new(&v, 4, sizeof(int));
  for (int i = 0; i < 100; i++) CTL_vector_push_back(&v, &i);
  CHECK(CTL_vector_size(&v) == 100);
  CHECK(*(int *)CTL_vector_front(&v) == 0);
  CHECK(*(int *)CTL_vector_back(&v) == 99);
  CHECK(*(int *)CTL_vector_at(&v, 42) == 42);

  CTL_vector_iterator it;
  CTL_vector_begin(&v, &it);
  CTL_vector_iterator_move(&it, 10, &it);
  int ten = -10;
  CTL_vector_insert(&v, &it, &ten);
  CHECK(CTL_vector_size(&v) == 101);
  CHECK(*(int *)CTL_vector_at(&v, 10) == -10);
  CHECK(*(int *)CTL_vector_at(&v, 11) == 10);
  CTL_vector_erase(&v, &it);
  CHECK(CTL_vector_size(&v) == 100);
  CHECK(*(int *)CTL_vector_at(&v, 10) == 10);

  CTL_vector_pop_back(&v);
  CHECK(*(int *)CTL_vector_back(&v) == 98);
  CTL_vector_clear(&v);
  CHECK(CTL_vector_size(&v) == 0);
  CTL_vector_push_back(&v, &ten);
  CHECK(CTL_vector_size(&v) == 1);
  CHECK(*(int *)CTL_vector_front(&v) == -10);
  CTL_vector_delete(&v);
  return 0;
}

static int test_list_and_deque(void) {
  CTL_list l;
  CTL_list_new(&l, sizeof(int));
  for (int i = 1; i <= 3; i++) CTL_list_push_back(&l, &i);
  int zero = 0;
  CTL_list_push_front(&l, &zero);
  CHECK(CTL_list_size(&l) == 4);
  CHECK(*(int *)CTL_list_front(&l) == 0);
  CHECK(*(int *)CTL_list_back(&l) == 3);
  CTL_list_pop_front(&l);
  CTL_list_pop_back(&l);
  CHECK(CTL_list_size(&l) == 2);
  CHECK(*(int *)CTL_list_front(&l) == 1);
  CHECK(*(int *)CTL_list_back(&l) == 2);
  CTL_list_delete(&l);

  CTL_list s;
  CTL_list_new(&s, sizeof(int));
  int in[] = {5, 3, 9, 1, 7};
  for (int i = 0; i < 5; i++) CTL_list_push_back(&s, &in[i]);
  CTL_list_sort(&s, greater_int); /* ascending: list merge takes a greater-than predicate */
  int sorted[] = {1, 3, 5, 7, 9};
  CTL_list_iterator it;
  CTL_list_begin(&s, &it);
  for (int i = 0; i < 5; i++) {
    CHECK(*(int *)it.data == sorted[i]);
    CTL_list_iterator_move(&it, 1, &it);
  }
  CTL_list_delete(&s);

  CTL_deque d = {0}; /* CTL_deque_new leaves size uninitialized */
  CTL_deque_new(&d, 8, sizeof(int));
  for (int i = 0; i < 50; i++) {
    int back = i, front = -i - 1;
    CTL_deque_push_back(&d, &back);
    CTL_deque_push_front(&d, &front);
  }
  CHECK(CTL_deque_size(&d) == 100);
  CHECK(*(int *)CTL_deque_front(&d) == -50);
  CHECK(*(int *)CTL_deque_back(&d) == 49);
  CHECK(*(int *)CTL_deque_at(&d, 50) == 0);
  CTL_deque_pop_front(&d);
  CTL_deque_pop_back(&d);
  CHECK(CTL_deque_size(&d) == 98);
  CHECK(*(int *)CTL_deque_front(&d) == -49);
  CTL_deque_delete(&d);
  return 0;
}

static int test_maps(void) {
  CTL_hash_map h;
  CTL_hash_map_new(&h, sizeof(int), sizeof(int));
  for (int k = 0; k < 10; k++) {
    int val = k * k;
    CHECK(CTL_hash_map_insert(&h, &k, &val) == 0);
  }
  int dup = 3, other = 0;
  CHECK(CTL_hash_map_insert(&h, &dup, &other) == -1);
  CHECK(CTL_hash_map_size(&h) == 10);
  CTL_hash_map_iterator it;
  for (int k = 0; k < 10; k++) {
    CHECK(CTL_hash_map_find(&h, &k, &it) == 0);
    CHECK(*(int *)it.data == k * k);
  }
  CTL_hash_map_find(&h, &dup, &it);
  CTL_hash_map_erase(&h, &it);
  CHECK(CTL_hash_map_find(&h, &dup, &it) == -1);
  CHECK(CTL_hash_map_size(&h) == 9);

  int sum = 0;
  CTL_hash_map_iterator end;
  CTL_hash_map_begin(&h, &it);
  CTL_hash_map_end(&h, &end);
  while (!CTL_hash_map_iterator_equal(&it, &end)) {
    sum += *(int *)it.data;
    CTL_hash_map_iterator_next(&it, &it);
  }
  CHECK(sum == 285 - 9);
  CTL_hash_map_delete(&h);

  CTL_tree_map t;
  CTL_tree_map_key key = {sizeof(int), equal_int, greater_int};
  CTL_tree_map_new(&t, &key, sizeof(int));
  int keys[] = {50, 20, 80, 10, 30, 70, 90, 60};
  for (int i = 0; i < 8; i++) {
    int val = keys[i] * 2;
    CTL_tree_map_insert(&t, &keys[i], &val);
  }
  CHECK(CTL_tree_map_size(&t) == 8);
  CTL_tree_map_iterator ti, tend;
  CTL_tree_map_begin(&t, &ti);
  CTL_tree_map_end(&t, &tend);
  int ordered[] = {10, 20, 30, 50, 60, 70, 80, 90};
  for (int i = 0; i < 8; i++) {
    CHECK(*(int *)ti.key == ordered[i]);
    CHECK(*(int *)ti.data == ordered[i] * 2);
    CTL_tree_map_iterator_move(&ti, 1, &ti);
  }
  CHECK(CTL_tree_map_iterator_equal(&ti, &tend));
  int want = 70;
  CTL_tree_map_find(&t, &want, &ti);
  CHECK(*(int *)ti.data == 140);
  CTL_tree_map_erase(&t, &ti);
  CHECK(CTL_tree_map_size(&t) == 7);
  want = 25;
  CTL_tree_map_find(&t, &want, &ti);
  CHECK(ti.data == NULL);
  CTL_tree_map_delete(&t);
  return 0;
}

static int test_adapters(void) {
  CTL_stack st;
  CTL_stack_new(&st, CTL_vector_functions(), 8, sizeof(int));
  for (int i = 0; i < 20; i++) CTL_stack_push(&st, &i);
  CHECK(*(int *)CTL_stack_top(&st) == 19);
  CTL_stack_pop(&st);
  CHECK(*(int *)CTL_stack_top(&st) == 18);
  CHECK(CTL_stack_size(&st) == 19);
  CTL_stack_delete(&st);

  CTL_queue q = {0};
  CTL_queue_new(&q, CTL_deque_functions(), 8, sizeof(int));
  for (int i = 0; i < 20; i++) CTL_queue_push(&q, &i);
  CHECK(*(int *)CTL_queue_front(&q) == 0);
  CHECK(*(int *)CTL_queue_back(&q) == 19);
  CTL_queue_pop(&q);
  CHECK(*(int *)CTL_queue_front(&q) == 1);
  CHECK(CTL_queue_size(&q) == 19);
  CTL_queue_delete(&q);

  CTL_priority_queue pq;
  CTL_priority_queue_new(&pq, less_int, CTL_vector_functions(), 8, sizeof(int));
  int in[] = {4, 8, 1, 9, 3, 7};
  for (int i = 0; i < 6; i++) CTL_priority_queue_push(&pq, &in[i]);
  int want[] = {9, 8, 7, 4, 3, 1};
  for (int i = 0; i < 6; i++) {
    CHECK(*(int *)CTL_priority_queue_top(&pq) == want[i]);
    CTL_priority_queue_pop(&pq);
  }
  CHECK(CTL_priority_queue_size(&pq) == 0);
  CTL_priority_queue_delete(&pq);
  return 0;
}

#define N 300

static int fill(int *a) {
  unsigned x = 12345;
  for (int i = 0; i < N; i++) {
    x = x * 1103515245u + 12345u;
    a[i] = (int)((x >> 16) % 1000);
  }
  return 0;
}

static int check_sorted(const int *a, int sum) {
  int got = 0;
  for (int i = 0; i < N; i++) got += a[i];
  CHECK(got == sum);
  for (int i = 1; i < N; i++) CHECK(a[i - 1] <= a[i]);
  return 0;
}

static int test_sorts(void) {
  int a[N], sum = 0;
  fill(a);
  for (int i = 0; i < N; i++) sum += a[i];

  CTL_quick_sort(a, a + N, sizeof(int), less_int);
  CHECK(check_sorted(a, sum) == 0);
  fill(a);
  CTL_intro_sort(a, a + N, sizeof(int), less_int);
  CHECK(check_sorted(a, sum) == 0);
  fill(a);
  CTL_merge_sort(a, a + N, sizeof(int), less_int);
  CHECK(check_sorted(a, sum) == 0);
  fill(a);
  CTL_heap_sort(a, a + N, sizeof(int), less_int);
  CHECK(check_sorted(a, sum) == 0);
  fill(a);
  CTL_insertion_sort(a, a + N, sizeof(int), less_int);
  CHECK(check_sorted(a, sum) == 0);
  fill(a);
  CTL_tim_sort(a, a + N, sizeof(int), less_int);
  CHECK(check_sorted(a, sum) == 0);
  return 0;
}

static int test_lockfree(void) {
#if UINTPTR_MAX > 0xffffffffu
  CTL_lockfree_stack st;
  CTL_lockfree_stack_new(&st, sizeof(int));
  for (int i = 0; i < 10; i++) CHECK(CTL_lockfree_stack_push(&st, &i) == 1);
  CHECK(CTL_lockfree_stack_size(&st) == 10);
  for (int i = 9; i >= 0; i--) {
    int v = -1;
    CHECK(CTL_lockfree_stack_pop(&st, &v) == 1);
    CHECK(v == i);
  }
  CHECK(CTL_lockfree_stack_empty(&st));
  CTL_lockfree_stack_delete(&st);

  CTL_lockfree_queue q;
  CTL_lockfree_queue_new(&q, sizeof(int));
  for (int i = 0; i < 10; i++) CHECK(CTL_lockfree_queue_push(&q, &i) == 1);
  CHECK(CTL_lockfree_queue_size(&q) == 10);
  for (int i = 0; i < 10; i++) {
    int v = -1;
    CHECK(CTL_lockfree_queue_pop(&q, &v) == 1);
    CHECK(v == i);
  }
  CHECK(CTL_lockfree_queue_empty(&q));
  CTL_lockfree_queue_delete(&q);
#endif

  CTL_lockfree_spsc_queue sq;
  CTL_lockfree_spsc_queue_new(&sq, 8, sizeof(int));
  int in[10], out[10];
  for (int i = 0; i < 10; i++) in[i] = i * 3;
  CHECK(CTL_lockfree_spsc_queue_push(&sq, in, 10) == 8);
  CHECK(CTL_lockfree_spsc_queue_size(&sq) == 8);
  CHECK(CTL_lockfree_spsc_queue_pop(&sq, out, 5) == 5);
  for (int i = 0; i < 5; i++) CHECK(out[i] == in[i]);
  CHECK(CTL_lockfree_spsc_queue_pop(&sq, out, 10) == 3);
  CHECK(out[2] == in[7]);
  CHECK(CTL_lockfree_spsc_queue_empty(&sq));
  CTL_lockfree_spsc_queue_delete(&sq);

  CTL_lockfree_mpmc_queue mq;
  CTL_lockfree_mpmc_queue_new(&mq, 8, sizeof(int));
  for (int i = 0; i < 20; i++) {
    int v = i + 100, got = -1;
    CHECK(CTL_lockfree_mpmc_queue_push(&mq, &v) == 1);
    CHECK(CTL_lockfree_mpmc_queue_pop(&mq, &got) == 1);
    CHECK(got == i + 100);
  }
  CHECK(CTL_lockfree_mpmc_queue_empty(&mq));
  CTL_lockfree_mpmc_queue_delete(&mq);
  return 0;
}

int main(void) {
  return test_vector() || test_list_and_deque() || test_maps() ||
         test_adapters() || test_sorts() || test_lockfree();
}
