// Consumer test from C++11: hb-cplusplus.hh's owning handles and the C API
// through C++ linkage, shaping with the empty font and with FreeType.
#include <hb-cplusplus.hh>
#include <hb-ft.h>
#include <hb.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>
#include <cstring>
#include <utility>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

}  // namespace

int main() {
  // shared_ptr shares one reference-counted object; unique_ptr owns it.
  hb::shared_ptr<hb_set_t> set{hb_set_create()};
  hb::shared_ptr<hb_set_t> copy = set;
  CHECK(copy.get() == set.get());
  hb_set_add(copy.get(), 42);
  CHECK(hb_set_has(set.get(), 42) && hb_set_get_population(set.get()) == 1);
  hb::shared_ptr<hb_set_t> moved = std::move(copy);
  CHECK(!copy && moved.get() == set.get());

  hb::unique_ptr<hb_buffer_t> buffer{hb_buffer_create()};
  CHECK(static_cast<bool>(buffer) && hb_buffer_allocation_successful(buffer.get()));
  hb_buffer_add_utf8(buffer.get(), "abc", -1, 0, -1);
  hb_buffer_guess_segment_properties(buffer.get());

  // A face built without tables has no glyphs: every character shapes to
  // .notdef.  (The inert empty face selects no shaper at all.)
  hb::unique_ptr<hb_face_t> face{hb_face_builder_create()};
  hb::unique_ptr<hb_font_t> empty{hb_font_create(face.get())};
  CHECK(hb_face_get_glyph_count(face.get()) == 0);
  CHECK(hb_shape_full(empty.get(), buffer.get(), nullptr, 0, nullptr));
  CHECK(hb_buffer_get_content_type(buffer.get()) == HB_BUFFER_CONTENT_TYPE_GLYPHS);
  unsigned length = 0;
  hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer.get(), &length);
  CHECK(length == 3);
  for (unsigned i = 0; i < length; ++i) CHECK(info[i].codepoint == 0 && info[i].cluster == i);

  // The vtable forwards user data and the empty object to the C API.
  static hb_user_data_key_t key;
  static int payload = 7;
  set.set_user_data(&key, &payload, nullptr, true);
  CHECK(moved.get_user_data(&key) == &payload);
  CHECK(hb_set_get_user_data(set.get(), &key) == &payload);
  CHECK(hb::shared_ptr<hb_font_t>::get_empty() == hb_font_get_empty());

  // FreeType's library handle and hb-ft's empty-face guard.
  FT_Library library = nullptr;
  CHECK(FT_Init_FreeType(&library) == 0);
  FT_Int major = 0, minor = 0, patch = 0;
  FT_Library_Version(library, &major, &minor, &patch);
  CHECK(major == 2 && minor >= 15);
  CHECK(FT_Done_FreeType(library) == 0);
  hb::unique_ptr<hb_font_t> font{hb_font_create(hb_face_get_empty())};
  hb_ft_font_set_funcs(font.get());
  CHECK(hb_ft_font_get_ft_face(font.get()) == nullptr);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("harfbuzz C++ consumer test passed");
  return 0;
}
