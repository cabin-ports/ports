#include <binn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what, int line)
{
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

/* Serialized copy of a container, as if received from the network. */
static std::vector<unsigned char> wire(binn *b)
{
  const unsigned char *p = static_cast<const unsigned char *>(binn_ptr(b));
  return std::vector<unsigned char>(p, p + binn_size(b));
}

int main()
{
  static unsigned char raw[4] = {0xDE, 0xAD, 0xBE, 0xEF};

  CHECK(std::strcmp(binn_version(), "3.0.0") == 0);

  binn *obj = binn_object();
  CHECK(binn_object_set_int32(obj, "id", 123));
  CHECK(binn_object_set_str(obj, "name", const_cast<char *>("Samsung Galaxy")));
  CHECK(binn_object_set_double(obj, "price", 299.5));
  CHECK(binn_object_set_int64(obj, "big", 5000000000LL));
  CHECK(binn_object_set_bool(obj, "ok", TRUE));
  CHECK(binn_object_set_blob(obj, "raw", raw, 4));
  std::vector<unsigned char> buf = wire(obj);
  binn_free(obj);

  int type = 0, count = 0, size = 0;
  CHECK(binn_is_valid(buf.data(), &type, &count, &size));
  CHECK(type == BINN_OBJECT && count == 6 && size == static_cast<int>(buf.size()));
  CHECK(binn_object_int32(buf.data(), "id") == 123);
  CHECK(std::strcmp(binn_object_str(buf.data(), "name"), "Samsung Galaxy") == 0);
  CHECK(binn_object_double(buf.data(), "price") == 299.5);
  CHECK(binn_object_int64(buf.data(), "big") == 5000000000LL);
  CHECK(binn_object_bool(buf.data(), "ok") == TRUE);
  int blob_size = 0;
  void *blob = binn_object_blob(buf.data(), "raw", &blob_size);
  CHECK(blob_size == 4 && std::memcmp(blob, raw, 4) == 0);

  int keys = 0;
  binn_iter iter;
  binn value;
  char key[256];
  binn_object_foreach(buf.data(), key, value) {
    keys += static_cast<int>(std::strlen(key));
  }
  CHECK(keys == 2 + 4 + 5 + 3 + 2 + 3);

  binn *list = binn_list();
  CHECK(binn_list_add_int32(list, 7));
  CHECK(binn_list_add_str(list, const_cast<char *>("x")));
  CHECK(binn_list_add_double(list, 2.25));
  CHECK(binn_count(list) == 3);
  CHECK(binn_list_int32(list, 1) == 7);
  CHECK(std::strcmp(binn_list_str(list, 2), "x") == 0);
  CHECK(binn_list_double(list, 3) == 2.25);
  binn_free(list);

  binn *map = binn_map();
  CHECK(binn_map_set_int32(map, 2, -42));
  CHECK(binn_map_set_str(map, 5, const_cast<char *>("five")));
  std::vector<unsigned char> mbuf = wire(map);
  binn_free(map);
  CHECK(binn_map_int32(mbuf.data(), 2) == -42);
  CHECK(std::strcmp(binn_map_str(mbuf.data(), 5), "five") == 0);
  int ids = 0, id = 0;
  binn_map_foreach(mbuf.data(), id, value) {
    ids += id;
  }
  CHECK(ids == 7);

  return failures == 0 ? 0 : 1;
}
