#include <CascLib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

/* A local BLTE file (CascReadFile.cpp, ParseBlteHeader): "BLTE", the header
   size (12 + 24 per frame, big endian), 0x0F, the frame count, then per frame
   its encoded size, content size and the MD5 of its encoded bytes.  Frame 1
   is stored ('N' + "hello "), frame 2 zlib-compressed ('Z' + "casclib"). */
static const unsigned char blte[] = {
    'B',  'L',  'T',  'E',  0x00, 0x00, 0x00, 0x3c, 0x0f, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x51, 0x09, 0x6e, 0x80,
    0x34, 0x14, 0x4a, 0x1b, 0x7f, 0xeb, 0x43, 0x85, 0x62, 0x7d, 0xfd, 0x40,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x56, 0xe8, 0x63,
    0xcb, 0x06, 0x92, 0xda, 0x25, 0x53, 0xb9, 0xc5, 0x3c, 0xe6, 0x0e, 0x3b,
    'N',  'h',  'e',  'l',  'l',  'o',  ' ',  'Z',  0x78, 0xda, 0x4b, 0x4e,
    0x2c, 0x4e, 0xce, 0xc9, 0x4c, 0x02, 0x00, 0x0b, 0x45, 0x02, 0xd2,
};
/* Offset of the first byte of frame 1's MD5. */
#define FRAME1_HASH 20

static int write_file(const char *path, const unsigned char *data,
                      size_t size) {
  FILE *out = fopen(path, "wb");
  if (out == NULL) return 0;
  if (fwrite(data, 1, size, out) != size) {
    fclose(out);
    return 0;
  }
  return fclose(out) == 0;
}

int main(void) {
  const char *dir = getenv("CABIN_PROFILE_DIR");
  char good[4096], bad[4096], missing[4096];
  unsigned char corrupt[sizeof blte];
  char buf[64];
  HANDLE h = NULL;
  ULONGLONG size = 0;
  DWORD got = 0;

  if (dir == NULL || strlen(dir) + 40 > sizeof good) {
    fprintf(stderr, "FAIL: CABIN_PROFILE_DIR\n");
    return 1;
  }
  sprintf(good, "%s/casclib-probe-c-good.blte", dir);
  sprintf(bad, "%s/casclib-probe-c-bad.blte", dir);
  sprintf(missing, "%s/casclib-probe-c-no-storage", dir);

  check(CASCLIB_VERSION == 0x0300, "CASCLIB_VERSION");
  check(strcmp(CASCLIB_VERSION_STRING, "3.0") == 0, "CASCLIB_VERSION_STRING");
  check(strcmp(CascCdnGetDefault(), "http://us.patch.battle.net:1119") == 0,
        "CascCdnGetDefault");

  check(!CascOpenLocalFile(NULL, 0, &h), "CascOpenLocalFile(NULL) fails");
  check(GetCascError() == ERROR_INVALID_PARAMETER,
        "CascOpenLocalFile(NULL) sets ERROR_INVALID_PARAMETER");

  h = NULL;
  check(!CascOpenStorage(missing, 0, &h), "CascOpenStorage(missing) fails");
  check(GetCascError() == ERROR_FILE_NOT_FOUND,
        "CascOpenStorage(missing) sets ERROR_FILE_NOT_FOUND");
  check(h == NULL, "CascOpenStorage(missing) returns no handle");

  check(write_file(good, blte, sizeof blte), "write good BLTE file");
  h = NULL;
  check(CascOpenLocalFile(good, CASC_STRICT_DATA_CHECK, &h) && h != NULL,
        "CascOpenLocalFile(good)");
  if (h != NULL) {
    check(CascGetFileSize64(h, &size) && size == 13, "CascGetFileSize64 == 13");
    memset(buf, 0, sizeof buf);
    check(CascReadFile(h, buf, sizeof buf, &got), "CascReadFile(good)");
    check(got == 13 && memcmp(buf, "hello casclib", 13) == 0,
          "decoded content is \"hello casclib\"");
    check(CascCloseFile(h), "CascCloseFile(good)");
  }

  /* A wrong frame hash fails a CASC_STRICT_DATA_CHECK read. */
  memcpy(corrupt, blte, sizeof blte);
  corrupt[FRAME1_HASH] ^= 0xff;
  check(write_file(bad, corrupt, sizeof corrupt), "write bad BLTE file");
  h = NULL;
  check(CascOpenLocalFile(bad, CASC_STRICT_DATA_CHECK, &h) && h != NULL,
        "CascOpenLocalFile(bad)");
  if (h != NULL) {
    got = 1;
    check(!CascReadFile(h, buf, sizeof buf, &got) && got == 0,
          "CascReadFile(bad) fails");
    check(GetCascError() == ERROR_FILE_CORRUPT,
          "CascReadFile(bad) sets ERROR_FILE_CORRUPT");
    CascCloseFile(h);
  }

  remove(good);
  remove(bad);
  if (failures != 0) return 1;
  printf("casclib C consumer OK\n");
  return 0;
}
