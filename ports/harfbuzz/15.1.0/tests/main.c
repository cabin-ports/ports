/* Consumer test from C99: the version and feature macros, the built-in
 * Unicode functions, buffers and segment properties, tags, languages,
 * features, sets, shaping with an OpenType font built in memory, buffer
 * (de)serialization, and the FreeType integration on that same font. */
#include <hb.h>
#include <hb-features.h>
#include <hb-ft.h>
#include <hb-ot.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdio.h>
#include <string.h>

#ifndef HB_HAS_FREETYPE
#error "hb-features.h must report the FreeType integration"
#endif
#if defined(HB_HAS_SUBSET) || defined(HB_HAS_GLIB) || defined(HB_HAS_ICU)
#error "hb-features.h reports a part this port does not build"
#endif

static int failures = 0;

static void check(int ok, const char *what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond) != 0, #cond, __LINE__)

/* A TrueType font with three empty glyphs: .notdef, 'A', and 'B', 1000
 * units per em, advances 500, 600, and 700. */
static const unsigned char head_table[54] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, /* version, revision */
    0x00, 0x00, 0x00, 0x00, 0x5F, 0x0F, 0x3C, 0xF5, /* checksum, magic */
    0x00, 0x03, 0x03, 0xE8,                         /* flags, upem 1000 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* created, modified */
    0, 0, 0, 0, 0, 0, 0, 0,                         /* bounding box */
    0x00, 0x00, 0x00, 0x08, 0x00, 0x02,             /* style, ppem, hint */
    0x00, 0x00, 0x00, 0x00,                         /* short loca, glyf 0 */
};
static const unsigned char hhea_table[36] = {
    0x00, 0x01, 0x00, 0x00, 0x03, 0x20, 0xFF, 0x38, /* ascender 800, -200 */
    0x00, 0x00, 0x02, 0xBC, 0, 0, 0, 0,             /* line gap, max 700 */
    0, 0, 0x00, 0x01, 0, 0, 0, 0,                   /* caret slope rise 1 */
    0, 0, 0, 0, 0, 0, 0, 0,                         /* reserved */
    0x00, 0x00, 0x00, 0x03,                         /* 3 metrics */
};
static const unsigned char maxp_table[32] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x03, /* version 1.0, 3 glyphs */
    0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x02, /* max zones 2 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char hmtx_table[12] = {
    0x01, 0xF4, 0x00, 0x00, 0x02, 0x58, 0x00, 0x00, 0x02, 0xBC, 0x00, 0x00,
};
static const unsigned char cmap_table[44] = {
    0x00, 0x00, 0x00, 0x01,                         /* 1 encoding record */
    0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0C, /* Windows Unicode BMP */
    0x00, 0x04, 0x00, 0x20, 0x00, 0x00,             /* format 4, length 32 */
    0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, /* 2 segments */
    0x00, 0x42, 0xFF, 0xFF, 0x00, 0x00,             /* end codes, pad */
    0x00, 0x41, 0xFF, 0xFF,                         /* start codes */
    0xFF, 0xC0, 0x00, 0x01,                         /* deltas: A -> 1 */
    0x00, 0x00, 0x00, 0x00,                         /* range offsets */
};
static const unsigned char loca_table[8] = {0};

static void add_table(hb_face_t *face, const char *tag, const unsigned char *data,
                      unsigned length) {
  hb_blob_t *blob = hb_blob_create((const char *)data, length, HB_MEMORY_MODE_READONLY,
                                   NULL, NULL);
  CHECK(hb_face_builder_add_table(face, hb_tag_from_string(tag, 4), blob));
  hb_blob_destroy(blob);
}

static hb_face_t *build_face(void) {
  hb_face_t *face = hb_face_builder_create();
  add_table(face, "head", head_table, sizeof head_table);
  add_table(face, "hhea", hhea_table, sizeof hhea_table);
  add_table(face, "maxp", maxp_table, sizeof maxp_table);
  add_table(face, "hmtx", hmtx_table, sizeof hmtx_table);
  add_table(face, "cmap", cmap_table, sizeof cmap_table);
  add_table(face, "loca", loca_table, sizeof loca_table);
  return face;
}

/* Shapes UTF-8 text left to right and checks glyphs, clusters, advances. */
static void check_shaping(hb_font_t *font, const char *text, unsigned count,
                          const hb_codepoint_t *glyphs, const unsigned *clusters,
                          const hb_position_t *advances, int line) {
  hb_buffer_t *buffer = hb_buffer_create();
  unsigned length = 0, i;
  hb_glyph_info_t *info;
  hb_glyph_position_t *pos;
  hb_buffer_add_utf8(buffer, text, -1, 0, -1);
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(font, buffer, NULL, 0);
  info = hb_buffer_get_glyph_infos(buffer, &length);
  pos = hb_buffer_get_glyph_positions(buffer, NULL);
  check(length == count, "glyph count", line);
  for (i = 0; i < length && i < count; ++i) {
    check(info[i].codepoint == glyphs[i], "glyph", line);
    check(info[i].cluster == clusters[i], "cluster", line);
    check(pos[i].x_advance == advances[i], "advance", line);
    check(pos[i].x_offset == 0 && pos[i].y_offset == 0 && pos[i].y_advance == 0,
          "offsets", line);
  }
  hb_buffer_destroy(buffer);
}

static void test_version_and_unicode(void) {
  hb_unicode_funcs_t *ufuncs = hb_unicode_funcs_get_default();
  hb_codepoint_t a = 0, b = 0, ab = 0;

  CHECK(HB_VERSION_MAJOR == 15 && HB_VERSION_MINOR == 1 && HB_VERSION_MICRO == 0);
  CHECK(strcmp(hb_version_string(), "15.1.0") == 0);
  CHECK(hb_version_atleast(15, 1, 0) && !hb_version_atleast(15, 1, 1));

  CHECK(hb_unicode_general_category(ufuncs, 'A') ==
        HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER);
  CHECK(hb_unicode_general_category(ufuncs, 0x0661) ==
        HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER);
  CHECK(hb_unicode_script(ufuncs, 0x0627) == HB_SCRIPT_ARABIC);
  CHECK(hb_unicode_script(ufuncs, 0x0E01) == HB_SCRIPT_THAI);
  CHECK(hb_unicode_mirroring(ufuncs, '(') == ')');
  CHECK(hb_unicode_combining_class(ufuncs, 0x0301) == HB_UNICODE_COMBINING_CLASS_ABOVE);
  CHECK(hb_unicode_compose(ufuncs, 'e', 0x0301, &ab) && ab == 0x00E9);
  CHECK(hb_unicode_decompose(ufuncs, 0x00E9, &a, &b) && a == 'e' && b == 0x0301);
  CHECK(!hb_unicode_compose(ufuncs, 'q', 0x0301, &ab));
}

static void test_buffer_properties(void) {
  hb_buffer_t *buffer = hb_buffer_create();
  hb_buffer_add_utf8(buffer, "Hello", -1, 0, -1);
  hb_buffer_guess_segment_properties(buffer);
  CHECK(hb_buffer_get_length(buffer) == 5);
  CHECK(hb_buffer_get_direction(buffer) == HB_DIRECTION_LTR);
  CHECK(hb_buffer_get_script(buffer) == HB_SCRIPT_LATIN);

  hb_buffer_reset(buffer);
  hb_buffer_add_utf8(buffer, "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85", -1, 0, -1);
  hb_buffer_guess_segment_properties(buffer);
  CHECK(hb_buffer_get_length(buffer) == 4);
  CHECK(hb_buffer_get_direction(buffer) == HB_DIRECTION_RTL);
  CHECK(hb_buffer_get_script(buffer) == HB_SCRIPT_ARABIC);
  hb_buffer_destroy(buffer);
}

static void test_tags_languages_features(void) {
  hb_feature_t feature;
  hb_tag_t script_tags[3], language_tags[3];
  unsigned script_count = 3, language_count = 3;
  char text[128];

  CHECK(hb_tag_from_string("liga", -1) == HB_TAG('l', 'i', 'g', 'a'));
  CHECK(hb_direction_from_string("rtl", -1) == HB_DIRECTION_RTL);
  CHECK(hb_script_from_string("Arab", -1) == HB_SCRIPT_ARABIC);
  CHECK(strcmp(hb_language_to_string(hb_language_from_string("EN_us", -1)), "en-us") == 0);

  CHECK(hb_feature_from_string("-liga", -1, &feature));
  CHECK(feature.tag == HB_TAG('l', 'i', 'g', 'a') && feature.value == 0);
  CHECK(hb_feature_from_string("kern[3:5]=2", -1, &feature));
  CHECK(feature.start == 3 && feature.end == 5 && feature.value == 2);
  hb_feature_to_string(&feature, text, sizeof text);
  CHECK(strcmp(text, "kern[3:5]=2") == 0);
  CHECK(!hb_feature_from_string("kern[", -1, &feature));

  hb_ot_tags_from_script_and_language(HB_SCRIPT_ARABIC, hb_language_from_string("ar", -1),
                                      &script_count, script_tags, &language_count,
                                      language_tags);
  CHECK(script_count >= 1 && script_tags[0] == HB_TAG('a', 'r', 'a', 'b'));
  CHECK(language_count >= 1 && language_tags[0] == HB_TAG('A', 'R', 'A', ' '));
}

static void test_set(void) {
  hb_set_t *set = hb_set_create();
  hb_codepoint_t next = HB_SET_VALUE_INVALID, first = 0, last = 0;
  hb_set_add_range(set, 10, 20);
  hb_set_del(set, 15);
  CHECK(hb_set_get_population(set) == 10);
  CHECK(hb_set_has(set, 14) && !hb_set_has(set, 15) && !hb_set_has(set, 21));
  CHECK(hb_set_get_min(set) == 10 && hb_set_get_max(set) == 20);
  CHECK(hb_set_next(set, &next) && next == 10);
  next = 14;
  CHECK(hb_set_next(set, &next) && next == 16);
  CHECK(hb_set_next_range(set, &first, &last) && first == 10 && last == 14);
  hb_set_destroy(set);
}

static void test_shaping(hb_face_t *face) {
  static const hb_codepoint_t aba_glyphs[] = {1, 2, 1};
  static const unsigned aba_clusters[] = {0, 1, 2};
  static const hb_position_t aba_advances[] = {600, 700, 600};
  static const hb_codepoint_t az_glyphs[] = {1, 0};
  static const unsigned az_clusters[] = {0, 1};
  static const hb_position_t az_advances[] = {600, 500};
  hb_font_t *font = hb_font_create(face);
  hb_codepoint_t glyph = 0;
  hb_buffer_t *buffer;
  hb_glyph_info_t *info;
  unsigned length = 0;
  char text[128];
  unsigned consumed = 0;
  const char *end = NULL;

  CHECK(hb_face_get_upem(face) == 1000);
  CHECK(hb_face_get_glyph_count(face) == 3);
  CHECK(hb_font_get_nominal_glyph(font, 'B', &glyph) && glyph == 2);
  CHECK(!hb_font_get_nominal_glyph(font, 'Z', &glyph));
  CHECK(hb_font_get_glyph_h_advance(font, 2) == 700);

  check_shaping(font, "ABA", 3, aba_glyphs, aba_clusters, aba_advances, __LINE__);
  check_shaping(font, "AZ", 2, az_glyphs, az_clusters, az_advances, __LINE__);

  /* Right to left, the glyphs come out in visual order. */
  buffer = hb_buffer_create();
  hb_buffer_add_utf8(buffer, "AB", -1, 0, -1);
  hb_buffer_set_direction(buffer, HB_DIRECTION_RTL);
  hb_buffer_set_script(buffer, HB_SCRIPT_LATIN);
  hb_shape(font, buffer, NULL, 0);
  info = hb_buffer_get_glyph_infos(buffer, &length);
  CHECK(length == 2 && info[0].codepoint == 2 && info[0].cluster == 1 &&
        info[1].codepoint == 1 && info[1].cluster == 0);

  /* The text serialization round-trips through the parser. */
  hb_buffer_reset(buffer);
  hb_buffer_add_utf8(buffer, "ABA", -1, 0, -1);
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(font, buffer, NULL, 0);
  hb_buffer_serialize_glyphs(buffer, 0, 3, text, sizeof text, &consumed, font,
                             HB_BUFFER_SERIALIZE_FORMAT_TEXT,
                             HB_BUFFER_SERIALIZE_FLAG_NO_GLYPH_NAMES);
  CHECK(strcmp(text, "[1=0+600|2=1+700|1=2+600]") == 0);
  hb_buffer_clear_contents(buffer);
  CHECK(hb_buffer_deserialize_glyphs(buffer, "[2=0+700|1=1+600]", -1, &end, font,
                                     HB_BUFFER_SERIALIZE_FORMAT_TEXT));
  info = hb_buffer_get_glyph_infos(buffer, &length);
  CHECK(*end == '\0' && length == 2 && info[0].codepoint == 2 && info[1].cluster == 1);
  CHECK(hb_buffer_get_glyph_positions(buffer, NULL)[1].x_advance == 600);
  hb_buffer_destroy(buffer);
  hb_font_destroy(font);
}

static void test_freetype(hb_face_t *face) {
  hb_blob_t *blob = hb_face_reference_blob(face);
  unsigned blob_length = 0;
  const char *data = hb_blob_get_data(blob, &blob_length);
  static const hb_codepoint_t ab_glyphs[] = {1, 2};
  static const unsigned ab_clusters[] = {0, 1};
  static const hb_position_t ab_ppem_advances[] = {600 * 64, 700 * 64};
  static const hb_position_t ab_advances[] = {600, 700};
  FT_Library library = NULL;
  FT_Face ft_face = NULL;
  hb_font_t *font;
  hb_face_t *ft_hb_face;
  hb_codepoint_t glyph = 0;
  int x_scale = 0, y_scale = 0;

  CHECK(blob_length > 12 && memcmp(data, "\x00\x01\x00\x00", 4) == 0);
  CHECK(FT_Init_FreeType(&library) == 0);
  CHECK(FT_New_Memory_Face(library, (const FT_Byte *)data, (FT_Long)blob_length, 0,
                           &ft_face) == 0);
  if (ft_face == NULL) {
    hb_blob_destroy(blob);
    FT_Done_FreeType(library);
    return;
  }

  /* A face over the FT_Face reads its tables through FreeType. */
  ft_hb_face = hb_ft_face_create_referenced(ft_face);
  CHECK(hb_face_get_upem(ft_hb_face) == 1000 && hb_face_get_glyph_count(ft_hb_face) == 3);
  hb_face_destroy(ft_hb_face);

  /* At 1000 pixels per em a font unit is one pixel, 64 in 26.6. */
  CHECK(FT_Set_Pixel_Sizes(ft_face, 0, 1000) == 0);
  font = hb_ft_font_create_referenced(ft_face);
  CHECK(hb_ft_font_get_ft_face(font) == ft_face);
  hb_font_get_scale(font, &x_scale, &y_scale);
  CHECK(x_scale == 1000 * 64 && y_scale == 1000 * 64);
  CHECK(hb_font_get_nominal_glyph(font, 'A', &glyph) && glyph == 1);
  CHECK(!hb_font_get_nominal_glyph(font, 'Z', &glyph));
  check_shaping(font, "AB", 2, ab_glyphs, ab_clusters, ab_ppem_advances, __LINE__);
  hb_font_destroy(font);
  FT_Done_Face(ft_face);
  hb_blob_destroy(blob);
  CHECK(FT_Done_FreeType(library) == 0);

  /* hb_ft_font_set_funcs opens the font's own blob with FreeType. */
  font = hb_font_create(face);
  hb_ft_font_set_funcs(font);
  CHECK(hb_ft_font_get_ft_face(font) != NULL);
  CHECK(hb_font_get_nominal_glyph(font, 'B', &glyph) && glyph == 2);
  check_shaping(font, "AB", 2, ab_glyphs, ab_clusters, ab_advances, __LINE__);
  hb_font_destroy(font);
}

int main(void) {
  hb_face_t *face = build_face();

  test_version_and_unicode();
  test_buffer_properties();
  test_tags_languages_features();
  test_set();
  test_shaping(face);
  test_freetype(face);
  hb_face_destroy(face);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("harfbuzz C consumer test passed");
  return 0;
}
