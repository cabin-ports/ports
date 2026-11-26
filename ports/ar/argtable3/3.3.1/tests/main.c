/* argtable3: parse typed options, report errors, and use the dynamic string
   and sub-command registry. */
#include <stdio.h>
#include <string.h>

#include <argtable3.h>

static int failures = 0;

static void check(int ok, int line, const char *what) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond) != 0, __LINE__, #cond)
#define A(s) ((char *)(s))

static int echo(int argc, char *argv[], arg_dstr_t res, void *ctx) {
  int i;
  for (i = 1; i < argc; ++i) {
    arg_dstr_catf(res, "%s%s", i > 1 ? " " : "", argv[i]);
  }
  return *(int *)ctx;
}

int main(void) {
  /* Typed options and one positional file. */
  {
    arg_lit_t *v = arg_litn("v", "verbose", 0, 3, "verbose");
    arg_int_t *c = arg_int1("c", "count", "<n>", "count");
    arg_dbl_t *r = arg_dbl0("r", "ratio", "<x>", "ratio");
    arg_str_t *n = arg_str1("n", "name", "<s>", "name");
    arg_file_t *f = arg_file1(NULL, NULL, "<file>", "input");
    arg_end_t *e = arg_end(20);
    void *t[6];
    char *argv[] = {A("prog"), A("-v"), A("-v"), A("--count=42"), A("-r"),
                    A("0.5"),  A("--name"), A("cabin"), A("dir/foo.bar"),
                    NULL};
    t[0] = v; t[1] = c; t[2] = r; t[3] = n; t[4] = f; t[5] = e;

    CHECK(arg_nullcheck(t) == 0);
    CHECK(arg_parse(9, argv, t) == 0);
    CHECK(v->count == 2);
    CHECK(c->count == 1 && c->ival[0] == 42);
    CHECK(r->count == 1 && r->dval[0] == 0.5);
    CHECK(n->count == 1 && strcmp(n->sval[0], "cabin") == 0);
    CHECK(f->count == 1 && strcmp(f->filename[0], "dir/foo.bar") == 0);
    CHECK(strcmp(f->basename[0], "foo.bar") == 0);
    CHECK(strcmp(f->extension[0], ".bar") == 0);
    arg_freetable(t, 6);
  }

  /* Hex and size-suffixed integers. */
  {
    arg_int_t *i = arg_intn("i", NULL, "<n>", 0, 3, "ints");
    arg_end_t *e = arg_end(20);
    void *t[2];
    char *argv[] = {A("prog"), A("-i"), A("0x1F"), A("-i"), A("2KB"),
                    A("-i"),   A("3MB"), NULL};
    t[0] = i; t[1] = e;

    CHECK(arg_parse(7, argv, t) == 0);
    CHECK(i->count == 3);
    CHECK(i->ival[0] == 31 && i->ival[1] == 2048 && i->ival[2] == 3145728);
    arg_freetable(t, 2);
  }

  /* Missing and malformed arguments are reported, with messages. */
  {
    arg_int_t *c = arg_int1("c", "count", "<n>", "count");
    arg_str_t *n = arg_str1("n", "name", "<s>", "name");
    arg_end_t *e = arg_end(20);
    void *t[3];
    arg_dstr_t ds = arg_dstr_create();
    char *none[] = {A("prog"), NULL};
    char *bad[] = {A("prog"), A("-c"), A("abc"), A("-n"), A("x"), NULL};
    t[0] = c; t[1] = n; t[2] = e;

    CHECK(arg_parse(1, none, t) == 2);
    arg_print_errors_ds(ds, e, "prog");
    CHECK(strstr(arg_dstr_cstr(ds), "prog: missing option -c") != NULL);
    CHECK(strstr(arg_dstr_cstr(ds), "prog: missing option -n") != NULL);
    arg_dstr_reset(ds);

    CHECK(arg_parse(5, bad, t) == 1);
    arg_print_errors_ds(ds, e, "prog");
    CHECK(strstr(arg_dstr_cstr(ds), "invalid argument \"abc\" to option -c")
          != NULL);
    arg_dstr_destroy(ds);
    arg_freetable(t, 3);
  }

  /* Dates through the embedded strptime, and regular expressions. */
  {
    arg_date_t *d = arg_date1("d", "date", "%Y-%m-%d", "<date>", "date");
    arg_rex_t *x = arg_rex1("x", "id", "^[a-z]+[0-9]+$", "<id>", 0, "id");
    arg_end_t *e = arg_end(20);
    void *t[3];
    char *good[] = {A("prog"), A("--date=2024-02-29"), A("--id=abc123"), NULL};
    char *bad[] = {A("prog"), A("--date=2024-13-01"), A("--id=123abc"), NULL};
    t[0] = d; t[1] = x; t[2] = e;

    CHECK(arg_parse(3, good, t) == 0);
    CHECK(d->tmval[0].tm_year == 124 && d->tmval[0].tm_mon == 1 &&
          d->tmval[0].tm_mday == 29);
    CHECK(strcmp(x->sval[0], "abc123") == 0);

    CHECK(arg_parse(3, bad, t) == 2);
    arg_freetable(t, 3);
  }

  /* Dynamic strings. */
  {
    arg_dstr_t ds = arg_dstr_create();
    arg_dstr_catf(ds, "%d-%s", 7, "x");
    arg_dstr_catc(ds, '!');
    CHECK(strcmp(arg_dstr_cstr(ds), "7-x!") == 0);
    arg_dstr_destroy(ds);
  }

  /* The sub-command registry, a hash table underneath. */
  {
    int status = 3;
    arg_dstr_t res = arg_dstr_create();
    char *argv[] = {A("echo"), A("a"), A("b"), NULL};

    arg_cmd_init();
    arg_cmd_register("echo", echo, "print its arguments", &status);
    CHECK(arg_cmd_count() == 1);
    CHECK(strcmp(arg_cmd_info("echo")->description, "print its arguments")
          == 0);
    CHECK(arg_cmd_dispatch("echo", 3, argv, res) == 3);
    CHECK(strcmp(arg_dstr_cstr(res), "a b") == 0);
    arg_cmd_unregister("echo");
    CHECK(arg_cmd_count() == 0);
    arg_cmd_uninit();
    arg_dstr_destroy(res);
  }

#ifdef _WIN32
  /* Backslash separates paths on Windows (the WIN32 define). */
  {
    arg_file_t *f = arg_file1(NULL, NULL, "<file>", "input");
    arg_end_t *e = arg_end(20);
    void *t[2];
    char *argv[] = {A("prog"), A("dir\\foo.bar"), NULL};
    t[0] = f; t[1] = e;
    CHECK(arg_parse(2, argv, t) == 0);
    CHECK(strcmp(f->basename[0], "foo.bar") == 0);
    arg_freetable(t, 2);
  }
#endif

  if (failures == 0) {
    puts("ok");
  }
  return failures != 0;
}
