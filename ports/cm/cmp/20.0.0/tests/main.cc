#include <cmp.h>

#include <stdio.h>
#include <string.h>

typedef struct {
  unsigned char data[256];
  size_t len;
  size_t pos;
} membuf;

static bool mem_read(cmp_ctx_t *ctx, void *out, size_t limit) {
  membuf *m = (membuf *)ctx->buf;
  if (m->len - m->pos < limit)
    return false;
  memcpy(out, m->data + m->pos, limit);
  m->pos += limit;
  return true;
}

static size_t mem_write(cmp_ctx_t *ctx, const void *in, size_t count) {
  membuf *m = (membuf *)ctx->buf;
  if (sizeof m->data - m->len < count)
    return 0;
  memcpy(m->data + m->len, in, count);
  m->len += count;
  return count;
}

#define CHECK(cond)                                           \
  do {                                                        \
    if (!(cond)) {                                            \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                               \
    }                                                         \
  } while (0)

int main(void) {
  membuf m;
  cmp_ctx_t ctx;
  int64_t i64 = 0;
  uint64_t u64 = 0;
  uint32_t n = 0;
  char str[16];
  uint32_t size = sizeof str;

  m.len = 0;
  m.pos = 0;
  cmp_init(&ctx, &m, mem_read, NULL, mem_write);
  CHECK(cmp_version() == 20);

  CHECK(cmp_write_integer(&ctx, -5));
  CHECK(cmp_write_integer(&ctx, -1000));
  CHECK(cmp_write_uinteger(&ctx, 300));
  CHECK(cmp_write_str(&ctx, "hello", 5));
  CHECK(cmp_write_array(&ctx, 2));
  CHECK(cmp_write_uinteger(&ctx, 1));
  CHECK(cmp_write_uinteger(&ctx, 2));
  CHECK(cmp_write_map(&ctx, 1));
  CHECK(cmp_write_str(&ctx, "k", 1));
  CHECK(cmp_write_nil(&ctx));

  /* -5 is a negative fixint (1 byte), -1000 an int16 (3), 300 a uint16 (3),
     "hello" a fixstr (6), the array 3 bytes, the map 1 + 2 + 1 bytes. */
  CHECK(m.len == 1 + 3 + 3 + 6 + 3 + 4);
  CHECK(m.data[0] == 0xfb);
  CHECK(m.data[1] == 0xd1 && m.data[2] == 0xfc && m.data[3] == 0x18);
  CHECK(m.data[4] == 0xcd && m.data[5] == 0x01 && m.data[6] == 0x2c);
  CHECK(m.data[7] == 0xa5 && memcmp(m.data + 8, "hello", 5) == 0);

  CHECK(cmp_read_integer(&ctx, &i64) && i64 == -5);
  CHECK(cmp_read_integer(&ctx, &i64) && i64 == -1000);
  CHECK(cmp_read_uinteger(&ctx, &u64) && u64 == 300);
  CHECK(cmp_read_str(&ctx, str, &size) && size == 5 && strcmp(str, "hello") == 0);
  CHECK(cmp_read_array(&ctx, &n) && n == 2);
  CHECK(cmp_read_uinteger(&ctx, &u64) && u64 == 1);
  CHECK(cmp_read_uinteger(&ctx, &u64) && u64 == 2);
  CHECK(cmp_read_map(&ctx, &n) && n == 1);
  size = sizeof str;
  CHECK(cmp_read_str(&ctx, str, &size) && size == 1 && strcmp(str, "k") == 0);
  CHECK(cmp_read_nil(&ctx));
  CHECK(m.pos == m.len);

  /* Reading an integer where a string was written fails with "Invalid type". */
  m.len = 0;
  m.pos = 0;
  ctx.error = 0;
  CHECK(cmp_write_str(&ctx, "x", 1));
  CHECK(!cmp_read_uinteger(&ctx, &u64));
  CHECK(strcmp(cmp_strerror(&ctx), "Invalid type") == 0);

  /* A write the sink rejects reports an error as well. */
  m.len = sizeof m.data;
  ctx.error = 0;
  CHECK(!cmp_write_nil(&ctx));
  CHECK(strlen(cmp_strerror(&ctx)) > 0);
  return 0;
}
