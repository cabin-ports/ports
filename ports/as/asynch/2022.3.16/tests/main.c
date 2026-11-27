#include <stdio.h>

#include <async/async-sem.h>
#include <async/async.h>

static int failures;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                           \
    }                                                       \
  } while (0)

/* A counter that yields after each step. */
struct counter {
  async_state;
  int i;
  int sum;
};

static async count_to_three(struct counter *c) {
  async_begin(c);
  for (c->i = 1; c->i <= 3; ++c->i) {
    c->sum += c->i;
    async_yield;
  }
  async_end;
}

/* Waits for a flag that the driver sets between calls. */
struct waiter {
  async_state;
  int flag;
  int after;
};

static async wait_for_flag(struct waiter *w) {
  async_begin(w);
  await(w->flag);
  w->after = 1;
  async_exit;
  w->after = 2; /* unreachable */
  async_end;
}

/* Calls two nested subroutines and waits for both. */
struct parent {
  async_state;
  struct counter a;
  struct counter b;
  int finished;
};

static async run_both(struct parent *p) {
  async_begin(p);
  async_init(&p->a);
  async_init(&p->b);
  await(async_call(count_to_three, &p->a) & async_call(count_to_three, &p->b));
  p->finished = 1;
  async_end;
}

static int spy_calls;

static async finish(struct async *a) {
  async_begin(a);
  async_exit;
  async_end;
}

static async spy(struct async *a) {
  (void)a;
  ++spy_calls;
  return ASYNC_DONE;
}

/* One producer and one consumer share a semaphore. */
struct channel {
  struct async_sem sem;
  int produced;
  int consumed;
};

struct producer {
  async_state;
  struct channel *ch;
};

struct consumer {
  async_state;
  struct channel *ch;
};

static async produce(struct producer *p) {
  async_begin(p);
  while (p->ch->produced < 2) {
    ++p->ch->produced;
    signal_sem(&p->ch->sem);
    async_yield;
  }
  async_end;
}

static async consume(struct consumer *c) {
  async_begin(c);
  while (c->ch->consumed < 2) {
    await_sem(&c->ch->sem);
    ++c->ch->consumed;
  }
  async_end;
}

int main(void) {
  struct async a;
  struct counter c;
  struct waiter w;
  struct parent p;
  struct channel ch;
  struct producer pr;
  struct consumer co;
  int steps;

  /* async_yield suspends until the next call; async_end completes. */
  c.sum = 0;
  async_init(&c);
  CHECK(!(async_done(&c)));
  CHECK(count_to_three(&c) == ASYNC_CONT && c.sum == 1);
  CHECK(count_to_three(&c) == ASYNC_CONT && c.sum == 3);
  CHECK(count_to_three(&c) == ASYNC_CONT && c.sum == 6);
  CHECK(count_to_three(&c) == ASYNC_DONE && c.sum == 6);
  CHECK(async_done(&c));
  CHECK(count_to_three(&c) == ASYNC_DONE && c.sum == 6);

  /* await blocks until its condition holds; async_exit ends early. */
  w.flag = 0;
  w.after = 0;
  async_init(&w);
  CHECK(wait_for_flag(&w) == ASYNC_CONT && w.after == 0);
  CHECK(wait_for_flag(&w) == ASYNC_CONT && w.after == 0);
  w.flag = 1;
  CHECK(wait_for_flag(&w) == ASYNC_DONE && w.after == 1);
  CHECK(async_done(&w));

  /* Nested subroutines run to completion under async_call. */
  p.a.sum = 0;
  p.b.sum = 0;
  p.finished = 0;
  async_init(&p);
  for (steps = 1; run_both(&p) == ASYNC_CONT; ++steps) {
    CHECK(!p.finished);
  }
  CHECK(steps == 4);
  CHECK(p.finished && p.a.sum == 6 && p.b.sum == 6);

  /* async_call does not call a subroutine that is already done. */
  async_init(&a);
  CHECK(async_call(spy, &a) && spy_calls == 1);
  CHECK(finish(&a) == ASYNC_DONE);
  CHECK(async_call(spy, &a) && spy_calls == 1);

  /* The counting semaphore orders consumer behind producer. */
  ch.produced = 0;
  ch.consumed = 0;
  ch.sem.count = 0;
  pr.ch = &ch;
  co.ch = &ch;
  async_init(&pr);
  async_init(&co);
  init_sem(&ch.sem, 0);
  CHECK(consume(&co) == ASYNC_CONT && ch.consumed == 0);
  CHECK(produce(&pr) == ASYNC_CONT && ch.produced == 1);
  CHECK(consume(&co) == ASYNC_CONT && ch.consumed == 1 && ch.sem.count == 0);
  CHECK(produce(&pr) == ASYNC_CONT && ch.produced == 2);
  CHECK(consume(&co) == ASYNC_DONE && ch.consumed == 2);
  CHECK(produce(&pr) == ASYNC_DONE);

  return failures == 0 ? 0 : 1;
}
