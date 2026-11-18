#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include <nuklear.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

struct ui {
  int clicks, changes;
  nk_bool checked;
  struct nk_rect window, button, checkbox;
};

struct vertex {
  float position[2];
  float uv[2];
  nk_byte color[4];
};

/* Named, unlike NK_ALIGNOF's struct, which cl (C4116) and clang warn about. */
struct vertex_align {
  char c;
  struct vertex v;
};

static void check_utilities(void) {
  static const char text[] = "  -123abc";
  static const char fox[] = "The quick brown fox jumps over the lazy dog";
  char *end = NULL;
  struct nk_color c = nk_rgb_hex("#FF8000");
  CHECK(c.r == 255 && c.g == 128 && c.b == 0 && c.a == 255);
  c = nk_rgb_hex("1a2B3c");
  CHECK(c.r == 0x1a && c.g == 0x2b && c.b == 0x3c && c.a == 255);
  CHECK(nk_strtoi(text, &end) == -123 && end == text + 6);
  /* SMHasher's MurmurHash3_x86_32 results. */
  CHECK(nk_murmur_hash("", 0, 0) == 0);
  CHECK(nk_murmur_hash("hello", 5, 0) == 0x248bfa47u);
  CHECK(nk_murmur_hash(fox, (int)strlen(fox), 0) == 0x2e4ff723u);
  CHECK(nk_murmur_hash("ab", 2, 0x9747b28cu) == 0x74875592u);
  CHECK(nk_murmur_hash(fox, (int)strlen(fox), 0x9747b28cu) == 0x2fa826cdu);
}

static void frame(struct nk_context *ctx, struct ui *ui) {
  if (nk_begin(ctx, "Demo", nk_rect(10, 20, 200, 200),
               NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
    ui->window = nk_window_get_bounds(ctx);
    nk_layout_row_dynamic(ctx, 30, 1);
    ui->button = nk_widget_bounds(ctx);
    if (nk_button_label(ctx, "Press"))
      ++ui->clicks;
    nk_layout_row_dynamic(ctx, 30, 1);
    ui->checkbox = nk_widget_bounds(ctx);
    ui->changes += nk_checkbox_label(ctx, "Check", &ui->checked);
    nk_fill_rect(nk_window_get_canvas(ctx), nk_rect(20, 160, 40, 20), 0,
                 nk_rgb(255, 128, 0));
  }
  nk_end(ctx);
}

/* One frame with the left button at (x, y), pressed or released. */
static void click_frame(struct nk_context *ctx, struct ui *ui, float x,
                        float y, int down) {
  nk_input_begin(ctx);
  nk_input_motion(ctx, (int)x, (int)y);
  nk_input_button(ctx, NK_BUTTON_LEFT, (int)x, (int)y, down);
  nk_input_end(ctx);
  frame(ctx, ui);
  nk_clear(ctx);
}

int main(void) {
  static const struct nk_draw_vertex_layout_element layout[] = {
      {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(struct vertex, position)},
      {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(struct vertex, uv)},
      {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(struct vertex, color)},
      {NK_VERTEX_LAYOUT_END}};
  struct nk_font_atlas atlas;
  struct nk_draw_null_texture tex_null;
  struct nk_context ctx;
  struct nk_convert_config config;
  struct nk_buffer cmds, vertices, indices;
  struct nk_memory_status used;
  const struct nk_draw_command *cmd;
  const struct vertex *v;
  struct nk_font *font;
  struct ui ui;
  const unsigned char *pixels;
  unsigned long lit = 0, elements = 0, k, opaque = 0, fringe = 0;
  int w = 0, h = 0, i;
  float x, y;

  check_utilities();

  /* Baking runs the embedded stb_truetype over the default font. */
  nk_font_atlas_init_default(&atlas);
  nk_font_atlas_begin(&atlas);
  font = nk_font_atlas_add_default(&atlas, 13.0f, NULL);
  CHECK(font != NULL);
  pixels = (const unsigned char *)nk_font_atlas_bake(&atlas, &w, &h,
                                                     NK_FONT_ATLAS_ALPHA8);
  CHECK(pixels != NULL && w > 0 && h > 0);
  if (font == NULL || pixels == NULL)
    return 1;
  for (i = 0; i < w * h; ++i)
    lit += pixels[i] != 0;
  CHECK(lit > 0);
  nk_font_atlas_end(&atlas, nk_handle_id(1), &tex_null);
  /* ProggyClean advances 7 px per glyph at 13 px. */
  CHECK(font->handle.height == 13);
  CHECK(font->handle.width(font->handle.userdata, 13, "M", 1) == 7 &&
        font->handle.width(font->handle.userdata, 13, "MM", 2) == 14);

  CHECK(nk_init_default(&ctx, &font->handle));
  memset(&ui, 0, sizeof ui);

  nk_input_begin(&ctx);
  nk_input_end(&ctx);
  frame(&ctx, &ui);
  CHECK(ui.window.x == 10 && ui.window.y == 20 && ui.window.w == 200 &&
        ui.window.h == 200);
  CHECK(ui.button.w > 0 && ui.button.h == 30 && ui.clicks == 0);
  CHECK(ui.checkbox.y == ui.button.y + 30 + ctx.style.window.spacing.y);

  /* Turn the frame into vertices and indices, as a renderer would. */
  memset(&config, 0, sizeof config);
  config.vertex_layout = layout;
  config.vertex_size = sizeof(struct vertex);
  config.vertex_alignment = offsetof(struct vertex_align, v);
  config.tex_null = tex_null;
  config.circle_segment_count = 22;
  config.curve_segment_count = 22;
  config.arc_segment_count = 22;
  config.global_alpha = 1.0f;
  config.shape_AA = NK_ANTI_ALIASING_ON;
  config.line_AA = NK_ANTI_ALIASING_ON;
  nk_buffer_init_default(&cmds);
  nk_buffer_init_default(&vertices);
  nk_buffer_init_default(&indices);
  CHECK(nk_convert(&ctx, &cmds, &vertices, &indices, &config) ==
        NK_CONVERT_SUCCESS);
  nk_draw_foreach(cmd, &ctx, &cmds) elements += cmd->elem_count;
  nk_buffer_info(&used, &indices);
  CHECK(elements > 0 && elements == used.allocated / sizeof(nk_draw_index));
  nk_buffer_info(&used, &vertices);
  CHECK(used.allocated > 0 && used.allocated % sizeof(struct vertex) == 0);
  /* The orange rectangle: four opaque vertices and four transparent
     anti-aliasing ones around its corners, all sampling the white texel. */
  v = (const struct vertex *)nk_buffer_memory_const(&vertices);
  for (k = 0; k < used.allocated / sizeof *v; ++k, ++v) {
    if (v->color[0] != 255 || v->color[1] != 128 || v->color[2] != 0 ||
        v->uv[0] != tex_null.uv.x || v->uv[1] != tex_null.uv.y ||
        v->position[0] < 19 || v->position[0] > 61 ||
        v->position[1] < 159 || v->position[1] > 181)
      continue;
    if (v->color[3] == 255)
      ++opaque;
    else if (v->color[3] == 0)
      ++fringe;
  }
  CHECK(opaque == 4 && fringe == 4);
  nk_buffer_free(&cmds);
  nk_buffer_free(&vertices);
  nk_buffer_free(&indices);
  nk_clear(&ctx);

  /* A button fires once, on the press; holding and releasing add nothing. */
  x = ui.button.x + ui.button.w / 2;
  y = ui.button.y + ui.button.h / 2;
  click_frame(&ctx, &ui, x, y, 1);
  CHECK(ui.clicks == 1);
  click_frame(&ctx, &ui, x, y, 1);
  click_frame(&ctx, &ui, x, y, 0);
  CHECK(ui.clicks == 1 && !ui.checked);

  x = ui.checkbox.x + ui.checkbox.h / 2;
  y = ui.checkbox.y + ui.checkbox.h / 2;
  click_frame(&ctx, &ui, x, y, 1);
  click_frame(&ctx, &ui, x, y, 1);
  click_frame(&ctx, &ui, x, y, 0);
  CHECK(ui.checked && ui.changes == 1 && ui.clicks == 1);

  nk_free(&ctx);
  nk_font_atlas_clear(&atlas);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("nuklear-c ok");
  return 0;
}
