// Drives libatomic_ops from C++ threads: four threads count with
// fetch-and-add and with a compare-and-swap loop, and pop nodes from one
// shared atomic_ops_gpl stack and push them back; afterwards the counts
// must be exact and the stack must hold every AO_malloc'ed node once.
#include <atomic_ops.h>
#include <atomic_ops_malloc.h>
#include <atomic_ops_stack.h>

#include <cstdio>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char *what)
{
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

struct node {
  AO_uintptr_t next;  // the link AO_stack_push takes comes first
  int value;
};

constexpr int kThreads = 4;
constexpr int kNodes = 64;
constexpr int kRounds = 20000;

AO_stack_t stack = AO_STACK_INITIALIZER;
volatile AO_t added = 0;
volatile AO_t swapped = 0;

void work()
{
  for (int i = 0; i < kRounds; ++i) {
    AO_fetch_and_add1(&added);
    AO_t seen = AO_load(&swapped);
    while (!AO_compare_and_swap(&swapped, seen, seen + 1))
      seen = AO_load(&swapped);
    if (AO_uintptr_t *link = AO_stack_pop(&stack))
      AO_stack_push(&stack, link);
  }
}

}  // namespace

int main()
{
  for (int i = 0; i < kNodes; ++i) {
    node *n = static_cast<node *>(AO_malloc(sizeof(node)));
    CHECK(n != nullptr);
    if (n == nullptr)
      return 1;
    n->value = i;
    AO_stack_push(&stack, &n->next);
  }

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t)
    threads.emplace_back(work);
  for (std::thread &t : threads)
    t.join();

  CHECK(AO_load(&added) == AO_t{kThreads} * kRounds);
  CHECK(AO_load(&swapped) == AO_t{kThreads} * kRounds);

  std::vector<int> seen(kNodes, 0);
  int popped = 0;
  while (AO_uintptr_t *link = AO_stack_pop(&stack)) {
    node *n = reinterpret_cast<node *>(link);
    CHECK(n->value >= 0 && n->value < kNodes);
    if (n->value >= 0 && n->value < kNodes)
      ++seen[n->value];
    ++popped;
    AO_free(n);
  }
  CHECK(popped == kNodes);
  for (int count : seen)
    CHECK(count == 1);

  std::printf("libatomic_ops C++: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
