#include <sass.h>

#include <stdio.h>
#include <string.h>

static int fail(const char *what, const char *got) {
  fprintf(stderr, "libsass: %s: [%s]\n", what, got ? got : "(null)");
  return 1;
}

static const char kSource[] =
    "$c: red; $w: 2px;\n"
    "@mixin box($p) { padding: $p; border: $w solid $c; }\n"
    ".a { color: $c; .b { @include box(4px); } }\n";

/* Compiles source with the given style; returns the status, and copies the
   output (or the error text) into out. */
static int compile(const char *source, enum Sass_Output_Style style,
                   char *out, size_t size) {
  struct Sass_Data_Context *data = sass_make_data_context(sass_copy_c_string(source));
  struct Sass_Context *ctx = sass_data_context_get_context(data);
  int status;
  sass_option_set_output_style(sass_data_context_get_options(data), style);
  status = sass_compile_data_context(data);
  snprintf(out, size, "%s",
           status == 0 ? sass_context_get_output_string(ctx)
                       : sass_context_get_error_text(ctx));
  sass_delete_data_context(data);
  return status;
}

int main(void) {
  char out[1024];

  if (strcmp(libsass_version(), "3.6.6") != 0)
    return fail("version", libsass_version());

  if (compile(kSource, SASS_STYLE_COMPRESSED, out, sizeof out) != 0)
    return fail("compile failed", out);
  if (strcmp(out, ".a{color:red}.a .b{padding:4px;border:2px solid red}\n") != 0)
    return fail("compressed output", out);

  if (compile(kSource, SASS_STYLE_COMPACT, out, sizeof out) != 0)
    return fail("compile failed", out);
  if (strcmp(out, ".a { color: red; }\n\n.a .b { padding: 4px; border: 2px solid red; }\n") != 0)
    return fail("compact output", out);

  if (compile(".a { color: $missing; }", SASS_STYLE_COMPRESSED, out, sizeof out) != 1)
    return fail("error status", out);
  if (strcmp(out, "Undefined variable: \"$missing\".") != 0)
    return fail("error text", out);

  puts("libsass ok");
  return 0;
}
