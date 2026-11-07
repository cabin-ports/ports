// yyjson from C++98: read a document, build a mutable one with copied
// strings, and write both back.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <yyjson.h>

namespace {

int failures = 0;

void check(bool ok, int line, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), __LINE__, #cond)

// The JSON yyjson_mut_write produces for doc, or "(null)" on failure.
std::string write(const yyjson_mut_doc *doc) {
  size_t len = 0;
  char *json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len);
  if (json == NULL) {
    return "(null)";
  }
  std::string out(json, len);
  std::free(json);
  return out;
}

}  // namespace

int main() {
  const std::string text = "{\"id\":7,\"tags\":[\"a\",\"b\"],\"pi\":3.25}";
  yyjson_doc *doc = yyjson_read(text.data(), text.size(), YYJSON_READ_NOFLAG);
  CHECK(doc != NULL);
  yyjson_val *root = yyjson_doc_get_root(doc);
  CHECK(yyjson_get_int(yyjson_obj_get(root, "id")) == 7);
  CHECK(yyjson_get_real(yyjson_obj_get(root, "pi")) == 3.25);
  yyjson_val *tags = yyjson_obj_get(root, "tags");
  CHECK(yyjson_arr_size(tags) == 2);
  CHECK(std::strcmp(yyjson_get_str(yyjson_arr_get(tags, 1)), "b") == 0);

  size_t len = 0;
  char *json = yyjson_write(doc, YYJSON_WRITE_NOFLAG, &len);
  CHECK(json != NULL && std::string(json, len) == text);
  std::free(json);

  // A mutable document that outlives the strings it was built from.
  yyjson_mut_doc *mdoc = yyjson_mut_doc_new(NULL);
  yyjson_mut_val *obj = yyjson_mut_obj(mdoc);
  yyjson_mut_doc_set_root(mdoc, obj);
  {
    std::string key = "tag";
    std::string value = "line\n\"quoted\"";
    CHECK(yyjson_mut_obj_add(obj, yyjson_mut_strcpy(mdoc, key.c_str()),
                             yyjson_mut_strcpy(mdoc, value.c_str())));
    yyjson_mut_val *tag_copy =
        yyjson_val_mut_copy(mdoc, yyjson_obj_get(root, "tags"));
    CHECK(yyjson_mut_obj_add_val(mdoc, obj, "tags", tag_copy));
  }
  yyjson_doc_free(doc);
  yyjson_mut_obj_add_real(mdoc, obj, "half", 0.5);
  CHECK(write(mdoc) ==
        "{\"tag\":\"line\\n\\\"quoted\\\"\",\"tags\":[\"a\",\"b\"],\"half\":0.5}");
  yyjson_mut_doc_free(mdoc);

  if (failures != 0) {
    return 1;
  }
  std::printf("yyjson %s: C++ API OK\n", YYJSON_VERSION_STRING);
  return 0;
}
