/* The test builds a private temporary directory with known entries, lists it sorted and
 * unsorted, opens single files and a subdirectory, checks the error paths, and removes everything
 * with the platform's own calls.  It creates no symlinks: whether tinydir reports a link or its
 * target depends on the feature macros in effect (tinydir.h:545-556). */
#include <tinydir.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static int failures = 0;

#ifndef _WIN32
/* Temporary CI probe: strict c89 on glibc does not declare symlink. */
int symlink(const char *target, const char *linkpath);
#endif

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

#define PATH_LEN 1024
static char root[PATH_LEN];

/* dir + "/" + name, which is also how tinydir spells an entry's path. */
static const char *join(char *out, const char *dir, const char *name) {
  out[0] = '\0';
  if (strlen(dir) + 1 + strlen(name) < PATH_LEN) {
    strcpy(out, dir);
    strcat(out, "/");
    strcat(out, name);
  }
  return out;
}

/* Appends n in decimal; C89 has no snprintf. */
static void append_number(char *out, unsigned long n) {
  char digits[24];
  size_t i = 0;
  do {
    digits[i++] = (char)('0' + n % 10);
    n /= 10;
  } while (n != 0);
  out += strlen(out);
  while (i > 0)
    *out++ = digits[--i];
  *out = '\0';
}

static int make_dir(const char *path) {
#ifdef _WIN32
  return CreateDirectoryA(path, NULL) != 0;
#else
  return mkdir(path, 0700) == 0;
#endif
}

static int remove_dir(const char *path) {
#ifdef _WIN32
  return RemoveDirectoryA(path) != 0;
#else
  return rmdir(path) == 0;
#endif
}

static int remove_file(const char *path) {
#ifdef _WIN32
  return DeleteFileA(path) != 0;
#else
  return unlink(path) == 0;
#endif
}

static int change_dir(const char *path) {
#ifdef _WIN32
  return SetCurrentDirectoryA(path) != 0;
#else
  return chdir(path) == 0;
#endif
}

static int save_cwd(char *out, size_t size) {
#ifdef _WIN32
  DWORD len = GetCurrentDirectoryA((DWORD)size, out);
  return len > 0 && len < size;
#else
  return getcwd(out, size) != NULL;
#endif
}

static int write_file(const char *path) {
  FILE *f = fopen(path, "wb");
  if (f == NULL)
    return 0;
  fputs("tinydir\n", f);
  return fclose(f) == 0;
}

static int make_root(void) {
  size_t n;
#ifdef _WIN32
  char base[MAX_PATH + 1];
  DWORD len = GetTempPathA((DWORD)sizeof base, base);
  if (len == 0 || len > MAX_PATH)
    return 0;
  strcpy(root, base);
#else
  const char *base = getenv("TMPDIR");
  if (base == NULL || *base == '\0')
    base = "/tmp";
  if (strlen(base) > PATH_LEN / 2)
    return 0;
  strcpy(root, base);
#endif
  n = strlen(root);
  while (n > 1 && (root[n - 1] == '/' || root[n - 1] == '\\'))
    root[--n] = '\0';
  strcat(root, "/tinydir-test-");
#ifdef _WIN32
  append_number(root, (unsigned long)GetCurrentProcessId());
#else
  append_number(root, (unsigned long)getpid());
#endif
  return make_dir(root);
}

struct entry {
  const char *name;
  int is_dir;
  const char *extension;
};

/* tinydir_open_sorted puts directories first, then sorts by name. */
static const struct entry listing[] = {
    {".", 1, ""},          {"..", 1, ""},        {"sub", 1, ""},
    {"alpha.txt", 0, "txt"}, {"beta", 0, ""},      {"beta.tar.gz", 0, "gz"},
};
#define LISTING_LEN (sizeof listing / sizeof listing[0])

static void check_file(const tinydir_file *f, const char *dir, const struct entry *e) {
  char path[PATH_LEN];
  CHECK(strcmp(f->name, e->name) == 0);
  CHECK(strcmp(f->path, join(path, dir, e->name)) == 0);
  CHECK(f->is_dir == e->is_dir);
  CHECK(f->is_reg == !e->is_dir);
  CHECK(strcmp(f->extension, e->extension) == 0);
}

/* The tinydir_file is only filled when a call succeeds. */
static void check_nth(const tinydir_dir *dir, size_t i, const char *in, const struct entry *e) {
  tinydir_file f;
  if (tinydir_readfile_n(dir, &f, i) != 0) {
    fprintf(stderr, "tinydir_readfile_n(%s, %lu) failed\n", in, (unsigned long)i);
    ++failures;
    return;
  }
  check_file(&f, in, e);
}

static void check_open(const char *path, const char *in, const struct entry *e) {
  tinydir_file f;
  if (tinydir_file_open(&f, path) != 0) {
    fprintf(stderr, "tinydir_file_open(%s) failed\n", path);
    ++failures;
    return;
  }
  check_file(&f, in, e);
}

static void check_sorted(void) {
  tinydir_dir dir;
  tinydir_file f;
  char sub[PATH_LEN];
  size_t i;
  static const struct entry inner = {"inner.c", 0, "c"};

  if (tinydir_open_sorted(&dir, root) != 0) {
    CHECK(0);
    return;
  }
  CHECK(strcmp(dir.path, root) == 0);
  CHECK(dir.n_files == LISTING_LEN);
  for (i = 0; i < LISTING_LEN && i < dir.n_files; ++i)
    check_nth(&dir, i, root, &listing[i]);
  errno = 0;
  CHECK(tinydir_readfile_n(&dir, &f, dir.n_files) == -1 && errno == ENOENT);

  /* Index 2 is "sub"; the call replaces the listing with the subdirectory's. */
  CHECK(tinydir_open_subdir_n(&dir, 2) == 0);
  CHECK(strcmp(dir.path, join(sub, root, "sub")) == 0);
  CHECK(dir.n_files == 3);
  if (dir.n_files == 3) {
    check_nth(&dir, 2, sub, &inner);
    errno = 0;
    CHECK(tinydir_open_subdir_n(&dir, 2) == -1 && errno == ENOENT);
  }
  tinydir_close(&dir);
  CHECK(dir.n_files == 0 && dir.has_next == 0);
}

static void check_unsorted(void) {
  tinydir_dir dir;
  int seen[LISTING_LEN];
  size_t i, count = 0;

  memset(seen, 0, sizeof seen);
  if (tinydir_open(&dir, root) != 0) {
    CHECK(0);
    return;
  }
  while (dir.has_next) {
    tinydir_file f;
    if (tinydir_readfile(&dir, &f) != 0) {
      CHECK(0);
      break;
    }
    ++count;
    for (i = 0; i < LISTING_LEN; ++i) {
      if (strcmp(f.name, listing[i].name) == 0) {
        ++seen[i];
        check_file(&f, root, &listing[i]);
      }
    }
    if (tinydir_next(&dir) != 0) {
      CHECK(0);
      break;
    }
  }
  CHECK(count == LISTING_LEN);
  for (i = 0; i < LISTING_LEN; ++i)
    CHECK(seen[i] == 1);
  errno = 0;
  CHECK(tinydir_next(&dir) == -1 && errno == ENOENT);
  tinydir_close(&dir);
}

static void check_file_open(void) {
  tinydir_file f;
  tinydir_dir dir;
  char path[PATH_LEN], cwd[PATH_LEN];

  /* "beta" and "beta.tar.gz" share a prefix: each must find its own entry. */
  check_open(join(path, root, "beta"), root, &listing[4]);
  check_open(join(path, root, "beta.tar.gz"), root, &listing[5]);
  check_open(join(path, root, "sub"), root, &listing[2]);

  /* A bare name is looked up in the current directory, spelled "./" + name. */
  if (save_cwd(cwd, sizeof cwd) && change_dir(root)) {
    check_open("beta", ".", &listing[4]);
    CHECK(change_dir(cwd));
  } else {
    CHECK(0);
  }

  /* tinydir_open drops a trailing slash. */
  if (tinydir_open(&dir, join(path, root, "")) == 0) {
    CHECK(strcmp(dir.path, root) == 0);
    tinydir_close(&dir);
  } else {
    CHECK(0);
  }

  errno = 0;
  CHECK(tinydir_file_open(&f, join(path, root, "missing.txt")) == -1 && errno == ENOENT);
  errno = 0;
  CHECK(tinydir_open(&dir, join(path, root, "missing")) == -1 && errno == ENOENT);
  errno = 0;
  CHECK(tinydir_open(&dir, "") == -1 && errno == EINVAL);
}

int main(void) {
  char a[PATH_LEN], b[PATH_LEN], c[PATH_LEN], s[PATH_LEN], i[PATH_LEN];
  int built;

  if (!make_root()) {
    fprintf(stderr, "cannot create the test directory\n");
    return 1;
  }
  join(a, root, "alpha.txt");
  join(b, root, "beta.tar.gz");
  join(c, root, "beta");
  join(s, root, "sub");
  join(i, s, "inner.c");
  built = write_file(a) && write_file(b) && write_file(c) && make_dir(s) && write_file(i);
  CHECK(built);
  if (built) {
    check_sorted();
    check_unsorted();
    check_file_open();
  }

#ifndef _WIN32
  {
    /* Temporary CI probe: a dangling symlink in the listed directory. */
    char link[PATH_LEN];
    tinydir_dir dir;
    int rc;
    CHECK(symlink("no-such-target", join(link, root, "dangling")) == 0);
    errno = 0;
    rc = tinydir_open_sorted(&dir, root);
    fprintf(stderr, "dangling-symlink probe: tinydir_open_sorted returned %d, errno %d\n", rc, errno);
    CHECK(rc == 0);
    if (rc == 0)
      tinydir_close(&dir);
    CHECK(remove_file(link));
  }
#endif

  CHECK(remove_file(i));
  CHECK(remove_dir(s));
  CHECK(remove_file(a));
  CHECK(remove_file(b));
  CHECK(remove_file(c));
  CHECK(remove_dir(root));

  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("tinydir: ok\n");
  return 0;
}
