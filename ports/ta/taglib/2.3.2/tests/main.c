#include <stdio.h>
#include <string.h>

#include <tag_c.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static size_t put(unsigned char *p, unsigned long v, int n) {
  int i;
  for (i = 0; i < n; ++i)
    p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
  return (size_t)n;
}

/* The fixture of main.cc: 100 ms of 8 kHz 16-bit mono PCM silence and an
 * ID3v2.4 tag whose TIT2 frame is zlib-compressed "Compressed". */
static size_t make_wav(unsigned char *buf) {
  static const unsigned char id3[] = {
    'I', 'D', '3', 4, 0, 0, 0, 0, 0, 33,
    'T', 'I', 'T', '2', 0, 0, 0, 23, 0x00, 0x09, 0, 0, 0, 11,
    0x78, 0xda, 0x63, 0x76, 0xce, 0xcf, 0x2d, 0x28, 0x4a, 0x2d,
    0x2e, 0x4e, 0x4d, 0x01, 0x00, 0x16, 0x21, 0x04, 0x19,
  };
  size_t n = 0;
  memcpy(buf + n, "RIFF", 4); n += 4;
  n += put(buf + n, 1688, 4);
  memcpy(buf + n, "WAVEfmt ", 8); n += 8;
  n += put(buf + n, 16, 4);
  n += put(buf + n, 1, 2);     /* PCM */
  n += put(buf + n, 1, 2);     /* channels */
  n += put(buf + n, 8000, 4);  /* sample rate */
  n += put(buf + n, 16000, 4); /* byte rate */
  n += put(buf + n, 2, 2);     /* block align */
  n += put(buf + n, 16, 2);    /* bits per sample */
  memcpy(buf + n, "data", 4); n += 4;
  n += put(buf + n, 1600, 4);
  memset(buf + n, 0, 1600); n += 1600;
  memcpy(buf + n, "ID3 ", 4); n += 4;
  n += put(buf + n, sizeof id3, 4);
  memcpy(buf + n, id3, sizeof id3); n += sizeof id3;
  buf[n++] = 0; /* pad byte of the odd-sized chunk */
  return n;
}

int main(void) {
  static unsigned char buf[1696];
  size_t size = make_wav(buf);
  TagLib_IOStream *stream;
  TagLib_File *file;
  check(size == sizeof buf, "fixture size");

  stream = taglib_memory_iostream_new((const char *)buf, (unsigned int)size);
  file = taglib_file_new_iostream(stream);
  check(file != NULL && taglib_file_is_valid(file), "WAV detected and valid");
  if (file != NULL && taglib_file_is_valid(file)) {
    const TagLib_AudioProperties *props = taglib_file_audioproperties(file);
    TagLib_Tag *tag = taglib_file_tag(file);
    char **values;

    check(taglib_audioproperties_samplerate(props) == 8000, "sample rate 8000");
    check(taglib_audioproperties_channels(props) == 1, "one channel");
    check(taglib_audioproperties_bitrate(props) == 128, "128 kb/s");
    check(strcmp(taglib_tag_title(tag), "Compressed") == 0,
          "zlib-compressed TIT2 frame decoded");

    taglib_tag_set_artist(tag, "Cabin");
    taglib_tag_set_year(tag, 2026);
    check(strcmp(taglib_tag_artist(tag), "Cabin") == 0, "artist set");
    check(taglib_tag_year(tag) == 2026, "year set");

    taglib_property_set(file, "ALBUM", "Ports");
    values = taglib_property_get(file, "ALBUM");
    check(values != NULL && values[0] != NULL &&
              strcmp(values[0], "Ports") == 0 && values[1] == NULL,
          "ALBUM property");
    taglib_property_free(values);
    check(taglib_file_save(file), "WAV saved");
    taglib_tag_free_strings();
  }
  if (file != NULL)
    taglib_file_free(file);
  taglib_iostream_free(stream);

  if (failures == 0)
    puts("taglib C binding: all checks passed");
  return failures == 0 ? 0 : 1;
}
