#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zip/zip.h>

static int ok = 1;

static int check(int cond, const char *what) {
    if (!cond) {
        printf("FAIL: %s\n", what);
        ok = 0;
    }
    return cond;
}

static const char digits[] = "123456789";
static char repeat[4096];

static int add(struct zip_t *zip, const char *name, const void *data, size_t size) {
    return zip_entry_open(zip, name) == 0 && (size == 0 || zip_entry_write(zip, data, size) == 0) &&
           zip_entry_close(zip) == 0;
}

/* An in-memory archive: digits.txt, data/ and data/repeat.bin. */
static int build(int level, char **buf, size_t *size) {
    struct zip_t *zip = zip_stream_open(NULL, 0, level, 'w');
    void *copy = NULL;
    int built = zip != NULL && add(zip, "digits.txt", digits, 9) && add(zip, "data/", NULL, 0) &&
                add(zip, "data\\repeat.bin", repeat, sizeof repeat) && zip_stream_copy(zip, &copy, size) > 0;
    if (zip != NULL) zip_stream_close(zip);
    *buf = copy;
    return built;
}

static int read_matches(struct zip_t *zip, const char *name, const void *data, size_t size) {
    void *out = NULL;
    size_t out_size = 0;
    int same = zip_entry_open(zip, name) == 0 && zip_entry_read(zip, &out, &out_size) == (ssize_t) size &&
               out_size == size && memcmp(out, data, size) == 0;
    free(out);
    zip_entry_close(zip);
    return same;
}

static void memory_round_trip(void) {
    char *buf = NULL, part[16];
    size_t size = 0;
    struct zip_t *zip;
    static char whole[sizeof repeat];

    if (!check(build(ZIP_DEFAULT_COMPRESSION_LEVEL, &buf, &size), "an archive is written in memory")) return;
    check(memcmp(buf, "PK\003\004", 4) == 0, "it starts with a local file header");
    zip = zip_stream_open(buf, size, 0, 'r');
    if (!check(zip != NULL, "the archive opens for reading")) {
        free(buf);
        return;
    }
    check(zip_entries_total(zip) == 3, "it has three entries");
    check(read_matches(zip, "digits.txt", digits, 9), "digits.txt reads back");
    check(zip_entry_open(zip, "digits.txt") == 0 && zip_entry_size(zip) == 9 && zip_entry_crc32(zip) == 0xCBF43926u,
          "digits.txt has its size and the CRC-32 check value");
    zip_entry_close(zip);
    check(read_matches(zip, "data/repeat.bin", repeat, sizeof repeat), "a backslash name is stored with a slash");
    if (check(zip_entry_open(zip, "data/repeat.bin") == 0, "data/repeat.bin opens")) {
        check(zip_entry_comp_size(zip) < sizeof repeat / 4, "repetitive data is deflated");
        check(zip_entry_noallocread(zip, whole, sizeof whole) == (ssize_t) sizeof whole &&
                  memcmp(whole, repeat, sizeof whole) == 0,
              "it reads into a caller's buffer");
        check(zip_entry_noallocread(zip, whole, sizeof whole - 1) < 0, "a buffer one byte short is refused");
        check(zip_entry_noallocreadwithoffset(zip, 1000, sizeof part, part) == (ssize_t) sizeof part &&
                  memcmp(part, repeat + 1000, sizeof part) == 0,
              "a slice reads from an offset");
        zip_entry_close(zip);
    }
    check(zip_entry_openbyindex(zip, 1) == 0 && strcmp(zip_entry_name(zip), "data/") == 0 && zip_entry_isdir(zip) == 1,
          "entry 1 is the directory data/");
    zip_entry_close(zip);
    check(zip_entry_open(zip, "missing.txt") == ZIP_ENOENT, "a missing entry is ZIP_ENOENT");
    check(strcmp(zip_strerror(ZIP_ENOENT), "entry not found") == 0, "zip_strerror describes it");
    zip_stream_close(zip);

    /* Delete digits.txt from a copy and read what is left. */
    zip = zip_stream_open(buf, size, 0, 'd');
    if (check(zip != NULL, "the archive opens for deletion")) {
        char *names[] = {"digits.txt"};
        void *pruned = NULL;
        size_t pruned_size = 0;
        check(zip_entries_delete(zip, names, 1) == 1 && zip_stream_copy(zip, &pruned, &pruned_size) > 0,
              "one entry is deleted");
        zip_stream_close(zip);
        zip = zip_stream_open(buf, size, 0, 'r');
        check(zip != NULL && zip_entries_total(zip) == 3 && read_matches(zip, "digits.txt", digits, 9),
              "the caller's buffer is left intact");
        if (zip != NULL) zip_stream_close(zip);
        zip = pruned != NULL ? zip_stream_open(pruned, pruned_size, 0, 'r') : NULL;
        if (check(zip != NULL, "the pruned archive opens")) {
            check(zip_entries_total(zip) == 2 && zip_entry_open(zip, "digits.txt") == ZIP_ENOENT &&
                      read_matches(zip, "data/repeat.bin", repeat, sizeof repeat),
                  "the deleted entry is gone and the others intact");
            zip_stream_close(zip);
        }
        free(pruned);
    }
    free(buf);
}

static void damaged(void) {
    char *buf = NULL, *hit;
    size_t size = 0, i;
    struct zip_t *zip;

    if (!check(build(0, &buf, &size), "a stored archive is written")) return;
    zip = zip_stream_open(buf, size - 1, 0, 'r');
    check(zip == NULL, "an archive missing its last byte does not open");
    if (zip != NULL) zip_stream_close(zip);

    for (hit = NULL, i = 0; i + 9 <= size && hit == NULL; ++i)
        if (memcmp(buf + i, digits, 9) == 0) hit = buf + i;
    if (check(hit != NULL, "level 0 stores digits.txt verbatim")) {
        void *out = NULL;
        size_t out_size = 0;
        hit[4] ^= 1;
        zip = zip_stream_open(buf, size, 0, 'r');
        check(zip != NULL && zip_entry_open(zip, "digits.txt") == 0 && zip_entry_read(zip, &out, &out_size) < 0,
              "a changed byte fails the CRC check");
        free(out);
        if (zip != NULL) zip_stream_close(zip);
    }
    free(buf);
}

/* Whether the file at path holds exactly the n bytes of text. */
static int file_is(const char *path, const char *text, size_t n) {
    char got[64];
    size_t len;
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    len = fread(got, 1, sizeof got, f);
    fclose(f);
    return len == n && memcmp(got, text, n) == 0;
}

/* Marks name's central directory entry as a Unix symlink, as `zip -y` writes
   one; kubazip's writer never does. */
static int mark_symlink(char *buf, size_t size, const char *name) {
    size_t len = strlen(name), i;
    for (i = 0; i + 46 + len <= size; ++i) {
        unsigned char *h = (unsigned char *) buf + i;
        if (memcmp(h, "PK\001\002", 4) == 0 && (size_t) h[28] == len && h[29] == 0 && memcmp(h + 46, name, len) == 0) {
            h[5] = 3; /* made by Unix */
            h[38] = h[39] = 0; /* mode 0120777 */
            h[40] = 0xFF;
            h[41] = 0xA1;
            return 1;
        }
    }
    return 0;
}

/* An archive holding target.txt and a symlink to it, or one climbing out. */
static char *link_archive(const char *link, const char *to, size_t *size) {
    struct zip_t *zip = zip_stream_open(NULL, 0, 0, 'w');
    void *copy = NULL;
    if (zip != NULL && add(zip, "port-probe-kubazip-target.txt", "pointed", 7) && add(zip, link, to, strlen(to)))
        zip_stream_copy(zip, &copy, size);
    if (zip != NULL) zip_stream_close(zip);
    if (copy != NULL && !mark_symlink(copy, *size, link)) {
        free(copy);
        copy = NULL;
    }
    return copy;
}

static void symlinks(const char *dir) {
    char link[1024], target[1024];
    size_t size = 0;
    struct zip_t *zip;
    char *buf;

    snprintf(link, sizeof link, "%s/port-probe-kubazip-link.txt", dir);
    snprintf(target, sizeof target, "%s/port-probe-kubazip-target.txt", dir);
    remove(link);
    remove(target);
    buf = link_archive("port-probe-kubazip-link.txt", "port-probe-kubazip-target.txt", &size);
    if (!check(buf != NULL, "an archive with a symlink entry is written")) return;
    zip = zip_stream_open(buf, size, 0, 'r');
    check(zip != NULL && zip_entry_open(zip, "port-probe-kubazip-link.txt") == 0 && zip_entry_issymlink(zip) == 1 &&
              zip_entry_close(zip) == 0,
          "the entry reads as a symlink");
    if (zip != NULL) zip_stream_close(zip);
    check(zip_stream_extract(buf, size, dir, NULL, NULL) == 0, "the symlink archive extracts");
#ifdef _WIN32
    check(file_is(link, "port-probe-kubazip-target.txt", 29), "without symlink support the link is a file holding its target");
#else
    check(file_is(link, "pointed", 7), "the link resolves to its target");
#endif
    remove(link);
    remove(target);
    free(buf);

#ifndef _WIN32
    buf = link_archive("port-probe-kubazip-climb", "../port-probe-kubazip-target.txt", &size);
    if (check(buf != NULL, "an archive with a climbing symlink is written")) {
        snprintf(link, sizeof link, "%s/port-probe-kubazip-climb", dir);
        check(zip_stream_extract(buf, size, dir, NULL, NULL) == ZIP_EINVENTNAME,
              "a symlink climbing out of the destination is refused");
        check(remove(link) != 0, "and not created");
        remove(target);
        free(buf);
    }
#endif
}

/* zip_open, append and extraction, in the probe's own output directory:
   `cabin run` keeps the caller's working directory. */
static void files(void) {
    const char *dir = getenv("CABIN_PROFILE_DIR");
    const char *sources[1];
    char path[1024], source[1024], out[1024], escaped[1024], landed[1024], leaf[1024];
    struct zip_t *zip;
    void *copy = NULL;
    char *buf;
    size_t size = 0;
    FILE *f;

    if (!check(dir != NULL, "CABIN_PROFILE_DIR is set")) return;
    snprintf(path, sizeof path, "%s/port-probe-kubazip-c.zip", dir);
    snprintf(source, sizeof source, "%s/port-probe-kubazip-source.txt", dir);
    snprintf(out, sizeof out, "%s/port-probe-kubazip-out.txt", dir);
    snprintf(landed, sizeof landed, "%s/port-probe-kubazip-escape.txt", dir);
    snprintf(escaped, sizeof escaped, "%s/../port-probe-kubazip-escape.txt", dir);
    snprintf(leaf, sizeof leaf, "%s/port-probe-kubazip-tree/sub/leaf.txt", dir);
    remove(path);

    zip = zip_open(path, ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');
    check(zip != NULL && add(zip, "first.txt", "one", 3), "a file archive is written");
    if (zip != NULL) zip_close(zip);
    zip = zip_open(path, ZIP_DEFAULT_COMPRESSION_LEVEL, 'a');
    check(zip != NULL && add(zip, "second.txt", "two", 3), "an entry is appended");
    if (zip != NULL) zip_close(zip);
    zip = zip_open(path, 0, 'r');
    if (check(zip != NULL, "the file archive opens for reading")) {
        check(zip_entries_total(zip) == 2 && read_matches(zip, "first.txt", "one", 3) &&
                  read_matches(zip, "second.txt", "two", 3),
              "both entries read back");
        zip_close(zip);
    }
    zip = zip_open(path, 0, 'd');
    if (check(zip != NULL, "the file archive opens for deletion")) {
        char *names[] = {"first.txt"};
        check(zip_entries_delete(zip, names, 1) == 1, "an entry is deleted from the file");
        zip_close(zip);
    }
    zip = zip_open(path, 0, 'r');
    check(zip != NULL && zip_entries_total(zip) == 1 && read_matches(zip, "second.txt", "two", 3),
          "the file holds only the remaining entry");
    if (zip != NULL) zip_close(zip);
    remove(path);

    zip = zip_open_with_password(path, ZIP_DEFAULT_COMPRESSION_LEVEL, 'w', "s3cret");
    check(zip != NULL && add(zip, "secret.txt", digits, 9), "a file archive is written with a password");
    if (zip != NULL) zip_close(zip);
    zip = zip_open_with_password(path, 0, 'r', "s3cret");
    check(zip != NULL && read_matches(zip, "secret.txt", digits, 9), "the password reads it back");
    if (zip != NULL) zip_close(zip);
    remove(path);

    /* zip_create stores a file under its base name. */
    f = fopen(source, "wb");
    if (f != NULL) {
        fputs("from disk", f);
        fclose(f);
    }
    sources[0] = source;
    check(zip_create(path, sources, 1) == 0, "zip_create archives a file");
    zip = zip_open(path, 0, 'r');
    check(zip != NULL && zip_entry_open(zip, "port-probe-kubazip-source.txt") == 0 && zip_entry_fread(zip, out) == 0 &&
              file_is(out, "from disk", 9),
          "zip_entry_fread writes the entry stored under the file's base name");
    if (zip != NULL) {
        zip_entry_close(zip);
        zip_close(zip);
    }
    remove(source);
    remove(out);
    remove(path);

    /* Extraction creates directories, and a name climbing out of the
       destination lands inside it. */
    zip = zip_stream_open(NULL, 0, ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');
    if (zip != NULL && add(zip, "port-probe-kubazip-tree/sub/leaf.txt", "leaf", 4) &&
        add(zip, "../port-probe-kubazip-escape.txt", "inside", 6))
        zip_stream_copy(zip, &copy, &size);
    if (zip != NULL) zip_stream_close(zip);
    buf = copy;
    if (check(buf != NULL, "an archive with nested and climbing names is written")) {
        check(zip_stream_extract(buf, size, dir, NULL, NULL) == 0, "it extracts");
        check(file_is(leaf, "leaf", 4), "a nested entry is written in new directories");
        check(file_is(landed, "inside", 6), "the climbing entry is written inside the destination");
        f = fopen(escaped, "rb");
        check(f == NULL, "nothing is written above it");
        if (f != NULL) fclose(f);
        remove(leaf);
        remove(landed);
        free(buf);
    }
    symlinks(dir);
}

int main(void) {
    size_t i;
    for (i = 0; i < sizeof repeat; ++i) repeat[i] = (char) ("kubazip "[i % 8] + (i / 512));
    memory_round_trip();
    damaged();
    files();
    if (!ok) return 1;
    puts("kubazip (C): ok");
    return 0;
}
