// Uses libimobiledevice-glue from C++, wrapping its headers in extern "C"
// as they have none: an OPACK round trip of a nested dictionary, the
// collection with FOREACH, thread_new with a captureless lambda, and the
// string helpers.
extern "C" {
#include <libimobiledevice-glue/collection.h>
#include <libimobiledevice-glue/opack.h>
#include <libimobiledevice-glue/thread.h>
#include <libimobiledevice-glue/utils.h>
}

#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static std::string text(plist_t node) {
  const char* s = plist_get_string_ptr(node, nullptr);
  return s ? s : "";
}

static std::string take(char* s) {
  std::string result = s ? s : "";
  std::free(s);
  return result;
}

int main() {
  // {"name": "glue", "items": ["a", "b"]}, encoded and decoded back.
  plist_t dict = plist_new_dict();
  plist_t items = plist_new_array();
  plist_array_append_item(items, plist_new_string("a"));
  plist_array_append_item(items, plist_new_string("b"));
  plist_dict_set_item(dict, "name", plist_new_string("glue"));
  plist_dict_set_item(dict, "items", items);
  unsigned char* buf = nullptr;
  unsigned int len = 0;
  opack_encode_from_plist(dict, &buf, &len);
  check(buf != nullptr && len > 0 && buf[0] == 0xE2, "two-entry dictionary");
  plist_t decoded = nullptr;
  check(opack_decode_to_plist(buf, len, &decoded) == 0 && decoded,
        "OPACK decodes");
  plist_t list = decoded ? plist_dict_get_item(decoded, "items") : nullptr;
  check(decoded && text(plist_dict_get_item(decoded, "name")) == "glue",
        "decoded string entry");
  check(list && plist_array_get_size(list) == 2 &&
            text(plist_array_get_item(list, 1)) == "b",
        "decoded array entry");
  std::free(buf);
  plist_free(decoded);
  plist_free(dict);

  std::string names[] = {"usbmuxd", "lockdownd", "afc"};
  struct collection col;
  collection_init(&col);
  for (std::string& entry : names) {
    collection_add(&col, &entry);
  }
  std::string joined;
  void* item = nullptr;
  FOREACH(item, &col) { joined += *static_cast<std::string*>(item) + ";"; }
  ENDFOREACH
  check(joined == "usbmuxd;lockdownd;afc;", "FOREACH");
  collection_free(&col);

  static int value = 0;
  int input = 21;
  THREAD_T thread;
  int started = thread_new(
      &thread,
      [](void* arg) -> void* {
        value = *static_cast<int*>(arg) * 2;
        return nullptr;
      },
      &input);
  check(started == 0, "thread_new with a lambda");
  if (started == 0) {
    check(thread_join(thread) == 0 && value == 42, "the lambda's thread");
    thread_free(thread);
  }

  check(take(string_concat("a", "b", "c", static_cast<char*>(nullptr))) ==
            "abc",
        "string_concat");
  check(take(string_build_path("usr", "lib", static_cast<char*>(nullptr))) ==
            "usr/lib",
        "string_build_path");

  if (failures == 0) {
    std::puts("libimobiledevice-glue: ok");
  }
  return failures == 0 ? 0 : 1;
}
