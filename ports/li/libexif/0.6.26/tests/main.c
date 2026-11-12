/* Builds EXIF data with an orientation, a camera make and the current date,
   saves it, then reads it back both directly and through ExifLoader from a
   JPEG stream (JFIF APP0, then the EXIF APP1), checking each value. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>
#include <libexif/exif-utils.h>

static int fail(const char *what) {
  fprintf(stderr, "%s\n", what);
  return 1;
}

static ExifEntry *add_entry(ExifData *d, ExifIfd ifd, ExifTag tag) {
  ExifEntry *e = exif_entry_new();
  exif_content_add_entry(d->ifd[ifd], e);
  exif_entry_initialize(e, tag);
  exif_entry_unref(e); /* The content holds the reference. */
  return e;
}

static int value_is(ExifData *d, ExifTag tag, const char *want) {
  char val[64];
  ExifEntry *e = exif_content_get_entry(d->ifd[EXIF_IFD_0], tag);
  if (e == NULL) return 0;
  exif_entry_get_value(e, val, sizeof val);
  if (strcmp(val, want) != 0) {
    fprintf(stderr, "%s: \"%s\", want \"%s\"\n", exif_tag_get_name(tag), val,
            want);
    return 0;
  }
  return 1;
}

int main(void) {
  static const unsigned char app0[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10,
                                       'J',  'F',  'I',  'F',  0x00, 0x01,
                                       0x01, 0x00, 0x00, 0x01, 0x00, 0x01,
                                       0x00, 0x00};
  ExifData *d = exif_data_new();
  ExifMem *mem = exif_mem_new_default();
  ExifEntry *e;
  ExifData *back;
  ExifLoader *loader;
  unsigned char *buf = NULL, *jpeg;
  unsigned int len = 0, jlen, i;
  const char *date;

  if (d == NULL || mem == NULL) return fail("allocation");
  exif_data_set_byte_order(d, EXIF_BYTE_ORDER_INTEL);

  e = add_entry(d, EXIF_IFD_0, EXIF_TAG_ORIENTATION);
  exif_set_short(e->data, EXIF_BYTE_ORDER_INTEL, 6);

  e = exif_entry_new_mem(mem);
  e->tag = EXIF_TAG_MAKE;
  e->format = EXIF_FORMAT_ASCII;
  e->components = e->size = 6;
  e->data = exif_mem_alloc(mem, e->size);
  if (e->data == NULL) return fail("allocation");
  memcpy(e->data, "Cabin", 6);
  exif_content_add_entry(d->ifd[EXIF_IFD_0], e);
  exif_entry_unref(e);

  /* Filled in through localtime_r or localtime_s: "YYYY:MM:DD HH:MM:SS". */
  e = add_entry(d, EXIF_IFD_0, EXIF_TAG_DATE_TIME);
  date = (const char *)e->data;
  if (e->format != EXIF_FORMAT_ASCII || e->components != 20 ||
      strlen(date) != 19 || date[4] != ':' || date[7] != ':' ||
      date[10] != ' ' || date[13] != ':' || date[16] != ':' ||
      strncmp(date, "2000", 4) < 0) {
    return fail("date");
  }

  exif_data_save_data(d, &buf, &len);
  if (buf == NULL || len < 14 || memcmp(buf, "Exif\0\0II*\0", 10) != 0) {
    return fail("saved header");
  }

  back = exif_data_new_from_data(buf, len);
  if (back == NULL || exif_data_get_byte_order(back) != EXIF_BYTE_ORDER_INTEL ||
      !value_is(back, EXIF_TAG_ORIENTATION, "Right-top") ||
      !value_is(back, EXIF_TAG_MAKE, "Cabin") ||
      !value_is(back, EXIF_TAG_DATE_TIME, date)) {
    return fail("reloaded data");
  }
  exif_data_unref(back);

  /* SOI and APP0, the APP1 segment with the saved data, and EOI. */
  jlen = sizeof app0 + 4 + len + 2;
  jpeg = malloc(jlen);
  if (jpeg == NULL) return fail("allocation");
  memcpy(jpeg, app0, sizeof app0);
  jpeg[sizeof app0] = 0xFF;
  jpeg[sizeof app0 + 1] = 0xE1;
  jpeg[sizeof app0 + 2] = (unsigned char)((len + 2) >> 8);
  jpeg[sizeof app0 + 3] = (unsigned char)(len + 2);
  memcpy(jpeg + sizeof app0 + 4, buf, len);
  jpeg[jlen - 2] = 0xFF;
  jpeg[jlen - 1] = 0xD9;

  loader = exif_loader_new();
  if (loader == NULL) return fail("allocation");
  for (i = 0; i < jlen; i += 5) {
    unsigned int n = jlen - i < 5 ? jlen - i : 5;
    if (!exif_loader_write(loader, jpeg + i, n)) break;
  }
  back = exif_loader_get_data(loader);
  if (back == NULL) return fail("exif_loader_get_data");
  e = exif_content_get_entry(back->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
  if (e == NULL ||
      exif_get_short(e->data, exif_data_get_byte_order(back)) != 6 ||
      !value_is(back, EXIF_TAG_MAKE, "Cabin") ||
      !value_is(back, EXIF_TAG_DATE_TIME, date)) {
    return fail("loaded data");
  }

  exif_data_unref(back);
  exif_loader_unref(loader);
  free(jpeg);
  free(buf);
  exif_mem_unref(mem);
  exif_data_unref(d);
  printf("libexif: save, reload and JPEG loader ok\n");
  return 0;
}
