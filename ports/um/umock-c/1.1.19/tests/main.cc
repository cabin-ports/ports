// Mocks the two dependencies of a small save function from C++, one taking a
// struct registered with the five-argument REGISTER_UMOCK_VALUE_TYPE, then
// fails each expected call in turn through umock_c's negative tests and
// checks that a call with the wrong file name is reported as a mismatch.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "umock_c/umock_c.h"
#include "umock_c/umock_c_negative_tests.h"
#include "umock_c/umocktypes_charptr.h"

struct BOX {
  int width;
  int height;
};

#define ENABLE_MOCKS
#include "umock_c/umock_c_prod.h"
MOCKABLE_FUNCTION(, int, open_file, const char*, path);
MOCKABLE_FUNCTION(, int, write_box, BOX, box);
#undef ENABLE_MOCKS

// The code under test: 0 on success, otherwise the step that failed.
static int save_box(const char* path, BOX box) {
  if (open_file(path) != 0) return 1;
  if (write_box(box) != 0) return 2;
  return 0;
}

static char* box_stringify(const BOX* value) {
  char* result = static_cast<char*>(std::malloc(32));
  if (result != nullptr) {
    std::snprintf(result, 32, "{%d,%d}", value->width, value->height);
  }
  return result;
}

static int box_are_equal(const BOX* left, const BOX* right) {
  return left->width == right->width && left->height == right->height;
}

static int box_copy(BOX* destination, const BOX* source) {
  *destination = *source;
  return 0;
}

static void box_free(BOX* value) { (void)value; }

static int umock_errors;

static void on_umock_c_error(UMOCK_C_ERROR_CODE error_code) {
  std::fprintf(stderr, "umock_c error %d\n", static_cast<int>(error_code));
  ++umock_errors;
}

static bool calls_are(const char* when, const char* expected,
                      const char* actual) {
  const char* e = umock_c_get_expected_calls();
  const char* a = umock_c_get_actual_calls();
  if (e == nullptr || a == nullptr || std::strcmp(e, expected) != 0 ||
      std::strcmp(a, actual) != 0) {
    std::fprintf(stderr, "%s: expected calls \"%s\", actual calls \"%s\"\n",
                 when, e != nullptr ? e : "(null)",
                 a != nullptr ? a : "(null)");
    return false;
  }
  return true;
}

int main() {
  if (umock_c_init(on_umock_c_error) != 0 ||
      umocktypes_charptr_register_types() != 0) {
    std::fprintf(stderr, "umock_c setup failed\n");
    return 1;
  }
  REGISTER_UMOCK_VALUE_TYPE(BOX, box_stringify, box_are_equal, box_copy,
                            box_free);
  REGISTER_GLOBAL_MOCK_RETURNS(open_file, 0, -1);
  REGISTER_GLOBAL_MOCK_RETURNS(write_box, 0, -1);

  const BOX box = {640, 480};
  STRICT_EXPECTED_CALL(open_file("out.bin"));
  STRICT_EXPECTED_CALL(write_box(box));
  if (save_box("out.bin", box) != 0 || !calls_are("happy path", "", "")) {
    return 1;
  }

  umock_c_reset_all_calls();
  if (umock_c_negative_tests_init() != 0) {
    std::fprintf(stderr, "umock_c_negative_tests_init failed\n");
    return 1;
  }
  STRICT_EXPECTED_CALL(open_file("out.bin"));
  STRICT_EXPECTED_CALL(write_box(box));
  umock_c_negative_tests_snapshot();
  const std::size_t count = umock_c_negative_tests_call_count();
  if (count != 2) {
    std::fprintf(stderr, "negative tests count %zu calls\n", count);
    return 1;
  }
  for (std::size_t i = 0; i < count; ++i) {
    umock_c_negative_tests_reset();
    umock_c_negative_tests_fail_call(i);
    const int result = save_box("out.bin", box);
    if (result != static_cast<int>(i) + 1) {
      std::fprintf(stderr, "failing call %zu gave %d\n", i, result);
      return 1;
    }
  }
  umock_c_negative_tests_deinit();

  // The unmatched open_file returns the global success value, so write_box
  // runs unexpected too.
  umock_c_reset_all_calls();
  STRICT_EXPECTED_CALL(open_file("out.bin"));
  if (save_box("other.bin", box) != 0 ||
      !calls_are("wrong file", "[open_file(\"out.bin\")]",
                 "[open_file(\"other.bin\")][write_box({640,480})]")) {
    return 1;
  }

  umock_c_deinit();
  if (umock_errors != 0) return 1;
  std::printf("umock_c: happy path, %zu negative cases and a mismatch ok\n",
              count);
  return 0;
}
