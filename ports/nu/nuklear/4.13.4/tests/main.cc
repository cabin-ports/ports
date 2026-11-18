#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include <nuklear.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

struct ui {
  int clicks = 0;
  nk_bool checked = 0;
  struct nk_rect window {}, button {}, checkbox {};
};

struct vertex {
  float position[2];
  float uv[2];
  nk_byte color[4];
};

void frame(nk_context &ctx, ui &state) {
  if (nk_begin(&ctx, "Demo", nk_rect(10, 20, 200, 200),
               NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
    state.window = nk_window_get_bounds(&ctx);
    nk_layout_row_dynamic(&ctx, 30, 1);
    state.button = nk_widget_bounds(&ctx);
    if (nk_button_label(&ctx, "Press"))
      ++state.clicks;
    nk_layout_row_dynamic(&ctx, 30, 1);
    state.checkbox = nk_widget_bounds(&ctx);
    nk_checkbox_label(&ctx, "Check", &state.checked);
    nk_fill_rect(nk_window_get_canvas(&ctx), nk_rect(20, 160, 40, 20), 0,
                 nk_rgb(255, 128, 0));
  }
  nk_end(&ctx);
}

// One frame with the left button at (x, y), pressed or released.
void click_frame(nk_context &ctx, ui &state, float x, float y, bool down) {
  nk_input_begin(&ctx);
  nk_input_motion(&ctx, static_cast<int>(x), static_cast<int>(y));
  nk_input_button(&ctx, NK_BUTTON_LEFT, static_cast<int>(x),
                  static_cast<int>(y), down);
  nk_input_end(&ctx);
  frame(ctx, state);
  nk_clear(&ctx);
}

} // namespace

int main() {
  struct nk_color c = nk_rgb_hex("#FF8000");
  CHECK(c.r == 255 && c.g == 128 && c.b == 0 && c.a == 255);
  CHECK(nk_murmur_hash("hello", 5, 0) == 0x248bfa47u);

  // Baking runs the embedded stb_truetype over the default font.
  nk_font_atlas atlas;
  nk_font_atlas_init_default(&atlas);
  nk_font_atlas_begin(&atlas);
  nk_font *font = nk_font_atlas_add_default(&atlas, 13.0f, nullptr);
  int w = 0, h = 0;
  const void *pixels =
      nk_font_atlas_bake(&atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
  CHECK(font != nullptr && pixels != nullptr && w > 0 && h > 0);
  if (font == nullptr || pixels == nullptr)
    return 1;
  // RGBA32 is white with the glyph coverage in alpha.
  const unsigned char *rgba = static_cast<const unsigned char *>(pixels);
  bool white = true;
  long lit = 0;
  for (int i = 0; i < w * h; ++i, rgba += 4) {
    white = white && rgba[0] == 255 && rgba[1] == 255 && rgba[2] == 255;
    lit += rgba[3] != 0;
  }
  CHECK(white && lit > 0);
  nk_draw_null_texture tex_null;
  nk_font_atlas_end(&atlas, nk_handle_id(1), &tex_null);

  nk_context ctx;
  CHECK(nk_init_default(&ctx, &font->handle));
  ui state;
  nk_input_begin(&ctx);
  nk_input_end(&ctx);
  frame(ctx, state);
  CHECK(state.window.x == 10 && state.window.y == 20 &&
        state.window.w == 200 && state.window.h == 200);

  static const nk_draw_vertex_layout_element layout[] = {
      {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(vertex, position)},
      {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(vertex, uv)},
      {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(vertex, color)},
      {NK_VERTEX_LAYOUT_END}};
  nk_convert_config config;
  std::memset(&config, 0, sizeof config);
  config.vertex_layout = layout;
  config.vertex_size = sizeof(vertex);
  config.vertex_alignment = NK_ALIGNOF(vertex);
  config.tex_null = tex_null;
  config.circle_segment_count = config.curve_segment_count =
      config.arc_segment_count = 22;
  config.global_alpha = 1.0f;
  nk_buffer cmds, vertices, indices;
  nk_buffer_init_default(&cmds);
  nk_buffer_init_default(&vertices);
  nk_buffer_init_default(&indices);
  CHECK(nk_convert(&ctx, &cmds, &vertices, &indices, &config) ==
        NK_CONVERT_SUCCESS);
  std::size_t elements = 0;
  const nk_draw_command *cmd;
  nk_draw_foreach(cmd, &ctx, &cmds) elements += cmd->elem_count;
  nk_memory_status used;
  nk_buffer_info(&used, &indices);
  CHECK(elements > 0 && elements == used.allocated / sizeof(nk_draw_index));
  // Without anti-aliasing the orange rectangle is four opaque vertices
  // sampling the white texel.
  nk_buffer_info(&used, &vertices);
  const vertex *v =
      static_cast<const vertex *>(nk_buffer_memory_const(&vertices));
  int orange = 0;
  for (std::size_t k = 0; k < used.allocated / sizeof *v; ++k, ++v)
    orange += v->color[0] == 255 && v->color[1] == 128 && v->color[2] == 0 &&
              v->color[3] == 255 && v->uv[0] == tex_null.uv.x &&
              v->uv[1] == tex_null.uv.y && v->position[0] >= 19 &&
              v->position[0] <= 61 && v->position[1] >= 159 &&
              v->position[1] <= 181;
  CHECK(orange == 4);
  nk_buffer_free(&cmds);
  nk_buffer_free(&vertices);
  nk_buffer_free(&indices);
  nk_clear(&ctx);

  // Pressing and releasing on the button counts one click; the same on the
  // checkbox ticks it.
  float x = state.button.x + state.button.w / 2;
  float y = state.button.y + state.button.h / 2;
  click_frame(ctx, state, x, y, true);
  click_frame(ctx, state, x, y, false);
  CHECK(state.clicks == 1 && !state.checked);
  x = state.checkbox.x + state.checkbox.h / 2;
  y = state.checkbox.y + state.checkbox.h / 2;
  click_frame(ctx, state, x, y, true);
  click_frame(ctx, state, x, y, false);
  CHECK(state.checked && state.clicks == 1);

  nk_free(&ctx);
  nk_font_atlas_clear(&atlas);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("nuklear ok");
  return 0;
}
