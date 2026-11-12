/* C consumer: ebml/c/libebml_t.h, the library's C header, checked for the
   widths and signedness of its fixed-size types, its open modes, and
   EBML_MIN. */
#include <ebml/c/libebml_t.h>

#include <stdio.h>

static int failures = 0;

#define CHECK(cond)                                                  \
  do {                                                               \
    if (!(cond)) {                                                   \
      printf("main.c:%d: check failed: %s\n", __LINE__, #cond);      \
      ++failures;                                                    \
    }                                                                \
  } while (0)

int main(void) {
  open_mode mode = MODE_CREATE;

  CHECK(sizeof(int8) == 1 && sizeof(int16) == 2 && sizeof(int32) == 4 && sizeof(int64) == 8);
  CHECK(sizeof(uint8) == 1 && sizeof(uint16) == 2 && sizeof(uint32) == 4 && sizeof(uint64) == 8);
  CHECK((int8)-1 < 0 && (int16)-1 < 0 && (int32)-1 < 0 && (int64)-1 < 0);
  CHECK((uint8)-1 == 255u && (uint16)-1 == 65535u && (uint32)-1 == 4294967295u &&
        (uint64)-1 == 18446744073709551615ull);
  CHECK(sizeof(binary) == 1 && (binary)-1 == 255u);
  CHECK(sizeof(filepos_t) == 8 && (filepos_t)-1 > 0);
  CHECK(MODE_READ == 0 && MODE_WRITE == 1 && MODE_CREATE == 2 && MODE_SAFE == 3);
  CHECK(mode == MODE_CREATE);
  CHECK(EBML_MIN(3, 7) == 3 && EBML_MIN(-2, -5) == -5);

  printf("libebml (C): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
