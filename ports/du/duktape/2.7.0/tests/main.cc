#include <duktape.h>

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                           \
    }                                                       \
  } while (0)

static duk_ret_t add(duk_context *ctx) {
  duk_push_number(ctx, duk_require_number(ctx, 0) + duk_require_number(ctx, 1));
  return 1;
}

int main(void) {
  duk_context *ctx = duk_create_heap_default();
  if (ctx == NULL) {
    fprintf(stderr, "duk_create_heap_default failed\n");
    return 1;
  }

  CHECK(DUK_VERSION == 20700L);

  CHECK(duk_peval_string(ctx, "1 + 2 * 3") == 0);
  CHECK(duk_get_int(ctx, -1) == 7);
  duk_pop(ctx);

  CHECK(duk_peval_string(ctx, "'abc'.toUpperCase() + 'def'.length") == 0);
  CHECK(strcmp(duk_get_string(ctx, -1), "ABC3") == 0);
  duk_pop(ctx);

  CHECK(duk_peval_string(ctx, "JSON.stringify({a: [1, 2, 3], b: 'x'})") == 0);
  CHECK(strcmp(duk_get_string(ctx, -1), "{\"a\":[1,2,3],\"b\":\"x\"}") == 0);
  duk_pop(ctx);

  /* libm: Math.floor and Math.pow. */
  CHECK(duk_peval_string(ctx, "Math.floor(7.9) + Math.pow(2, 10)") == 0);
  CHECK(duk_get_int(ctx, -1) == 1031);
  duk_pop(ctx);

  duk_push_c_function(ctx, add, 2);
  duk_put_global_string(ctx, "add");
  CHECK(duk_peval_string(ctx, "add(40, 2)") == 0);
  CHECK(duk_get_int(ctx, -1) == 42);
  duk_pop(ctx);

  CHECK(duk_peval_string(ctx, "throw new TypeError('boom')") != 0);
  CHECK(strcmp(duk_safe_to_string(ctx, -1), "TypeError: boom") == 0);
  duk_pop(ctx);

  CHECK(duk_get_top(ctx) == 0);
  duk_destroy_heap(ctx);
  return failures == 0 ? 0 : 1;
}
