#include <binn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what, int line)
{
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond) ? 1 : 0, #cond, __LINE__)

/* Serialized copy of a container, as if received from the network. */
static void *wire(binn *b)
{
  void *buf = malloc((size_t)binn_size(b));
  memcpy(buf, binn_ptr(b), (size_t)binn_size(b));
  return buf;
}

int main(void)
{
  static unsigned char raw[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  binn *obj, *list, *map, *inner;
  binn_iter iter;
  binn value;
  char key[256];
  int id, size, sum, type, count;
  void *buf, *blob;

  CHECK(strcmp(binn_version(), "3.0.0") == 0);

  /* Object: typed values survive serialization. */
  obj = binn_object();
  CHECK(binn_object_set_int32(obj, "id", 123));
  CHECK(binn_object_set_str(obj, "name", "Samsung Galaxy"));
  CHECK(binn_object_set_double(obj, "price", 299.5));
  CHECK(binn_object_set_int64(obj, "big", 5000000000LL));
  CHECK(binn_object_set_bool(obj, "ok", TRUE));
  CHECK(binn_object_set_blob(obj, "raw", raw, 4));
  buf = wire(obj);
  binn_free(obj);
  CHECK(binn_is_valid(buf, &type, &count, &size));
  CHECK(type == BINN_OBJECT && count == 6);
  CHECK(binn_count(buf) == 6);
  /* An informed count must match the buffer. */
  type = 0;
  count = 7;
  size = 0;
  CHECK(!binn_is_valid_ex(buf, &type, &count, &size));
  CHECK(binn_object_int32(buf, "id") == 123);
  CHECK(strcmp(binn_object_str(buf, "name"), "Samsung Galaxy") == 0);
  CHECK(binn_object_double(buf, "price") == 299.5);
  CHECK(binn_object_int64(buf, "big") == 5000000000LL);
  CHECK(binn_object_bool(buf, "ok") == TRUE);
  blob = binn_object_blob(buf, "raw", &size);
  CHECK(size == 4 && memcmp(blob, raw, 4) == 0);
  CHECK(binn_object_int32(buf, "missing") == 0);
  sum = 0;
  binn_object_foreach(buf, key, value) {
    sum += (int)strlen(key);
  }
  CHECK(sum == 2 + 4 + 5 + 3 + 2 + 3);
  free(buf);

  /* List: positions are 1-based. */
  list = binn_list();
  CHECK(binn_list_add_int32(list, 7));
  CHECK(binn_list_add_str(list, "x"));
  CHECK(binn_list_add_double(list, 2.25));
  CHECK(binn_count(list) == 3);
  CHECK(binn_list_int32(list, 1) == 7);
  CHECK(strcmp(binn_list_str(list, 2), "x") == 0);
  CHECK(binn_list_double(list, 3) == 2.25);
  sum = 0;
  count = 0;
  binn_list_foreach(list, value) {
    ++count;
    if (value.type == BINN_STRING) sum += (int)strlen((char *)value.ptr);
  }
  CHECK(count == 3 && sum == 1);
  binn_free(list);

  /* Map with integer keys and a nested object. */
  map = binn_map();
  inner = binn_object();
  CHECK(binn_object_set_str(inner, "k", "v"));
  CHECK(binn_map_set_int32(map, 2, -42));
  CHECK(binn_map_set_str(map, 5, "five"));
  CHECK(binn_map_set_object(map, 10, inner));
  binn_free(inner);
  buf = wire(map);
  binn_free(map);
  CHECK(binn_map_int32(buf, 2) == -42);
  CHECK(strcmp(binn_map_str(buf, 5), "five") == 0);
  CHECK(strcmp(binn_object_str(binn_map_object(buf, 10), "k"), "v") == 0);
  sum = 0;
  binn_map_foreach(buf, id, value) {
    sum += id;
  }
  CHECK(sum == 17);
  free(buf);

  return failures == 0 ? 0 : 1;
}
