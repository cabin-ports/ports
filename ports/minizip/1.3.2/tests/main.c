/* The C consumer, through the stdio file functions (ioapi.c): decodes an
 * archive Info-ZIP's zip 3.0 wrote, rejects a damaged copy, then writes an
 * archive with stored, deflated, Zip64, and encrypted entries, appends to
 * it, and reads it all back. */
#include <minizip/unzip.h>
#include <minizip/zip.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TZ=UTC zip -X -D {-0 hello.txt | -9 deflate.txt | -9 -P cabin-ports
 * secret.txt}, then zip -z, with every file dated 2026-02-17 12:00. */
static const unsigned char reference_zip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x51, 0x5c, 0x63, 0x5a,
    0xa4, 0xd5, 0x19, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x68, 0x65,
    0x6c, 0x6c, 0x6f, 0x2e, 0x74, 0x78, 0x74, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x66, 0x72, 0x6f,
    0x6d, 0x20, 0x49, 0x6e, 0x66, 0x6f, 0x2d, 0x5a, 0x49, 0x50, 0x20, 0x33, 0x2e, 0x30, 0x2e, 0x0a,
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00, 0x60, 0x51, 0x5c, 0xf5, 0xfe,
    0x0f, 0x8d, 0x2f, 0x00, 0x00, 0x00, 0x80, 0x04, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x64, 0x65,
    0x66, 0x6c, 0x61, 0x74, 0x65, 0x2e, 0x74, 0x78, 0x74, 0x73, 0x4e, 0x4c, 0xca, 0xcc, 0x53, 0x48,
    0xce, 0xcf, 0x2b, 0x2e, 0xcd, 0x4d, 0x2d, 0x56, 0x48, 0x06, 0x71, 0x75, 0x0b, 0xf2, 0x8b, 0x4a,
    0x8a, 0xf5, 0x73, 0x33, 0xf3, 0x32, 0xab, 0x32, 0x0b, 0xf4, 0xb8, 0x9c, 0x47, 0xd5, 0x8c, 0xaa,
    0x19, 0x55, 0x33, 0xaa, 0x86, 0x06, 0x6a, 0x00, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x0b, 0x00,
    0x08, 0x00, 0x00, 0x60, 0x51, 0x5c, 0x02, 0x3b, 0x9e, 0x12, 0x3a, 0x00, 0x00, 0x00, 0xac, 0x00,
    0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74, 0x2e, 0x74, 0x78, 0x74,
    0x5a, 0xdc, 0x68, 0x00, 0x34, 0xa8, 0xaa, 0xea, 0x37, 0xaa, 0x58, 0x5d, 0x97, 0x35, 0xe4, 0x5f,
    0xc6, 0x4e, 0xd2, 0x4a, 0xaa, 0xa9, 0xe9, 0x92, 0x5f, 0x8a, 0x66, 0xbc, 0xd2, 0x75, 0x7b, 0x4c,
    0xc7, 0x25, 0x3e, 0x91, 0xf7, 0x9b, 0x3a, 0x50, 0xab, 0xc4, 0xf6, 0xec, 0x83, 0x17, 0xe1, 0x54,
    0xcc, 0x28, 0xcd, 0x51, 0xd0, 0x05, 0x65, 0xe3, 0x43, 0x47, 0x50, 0x4b, 0x07, 0x08, 0x02, 0x3b,
    0x9e, 0x12, 0x3a, 0x00, 0x00, 0x00, 0xac, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x1e, 0x03,
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x51, 0x5c, 0x63, 0x5a, 0xa4, 0xd5, 0x19, 0x00,
    0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xa4, 0x81, 0x00, 0x00, 0x00, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x2e, 0x74, 0x78,
    0x74, 0x50, 0x4b, 0x01, 0x02, 0x1e, 0x03, 0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00, 0x60, 0x51,
    0x5c, 0xf5, 0xfe, 0x0f, 0x8d, 0x2f, 0x00, 0x00, 0x00, 0x80, 0x04, 0x00, 0x00, 0x0b, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xa4, 0x81, 0x40, 0x00, 0x00, 0x00, 0x64,
    0x65, 0x66, 0x6c, 0x61, 0x74, 0x65, 0x2e, 0x74, 0x78, 0x74, 0x50, 0x4b, 0x01, 0x02, 0x1e, 0x03,
    0x14, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x00, 0x60, 0x51, 0x5c, 0x02, 0x3b, 0x9e, 0x12, 0x3a, 0x00,
    0x00, 0x00, 0xac, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0xa4, 0x81, 0x98, 0x00, 0x00, 0x00, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74, 0x2e, 0x74,
    0x78, 0x74, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0xa8, 0x00,
    0x00, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x29, 0x00, 0x72, 0x65, 0x66, 0x65, 0x72, 0x65, 0x6e, 0x63,
    0x65, 0x20, 0x61, 0x72, 0x63, 0x68, 0x69, 0x76, 0x65, 0x20, 0x66, 0x6f, 0x72, 0x20, 0x63, 0x61,
    0x62, 0x69, 0x6e, 0x2d, 0x70, 0x6f, 0x72, 0x74, 0x73, 0x2f, 0x6d, 0x69, 0x6e, 0x69, 0x7a, 0x69,
    0x70};
#define REFERENCE_COMMENT "reference archive for cabin-ports/minizip"
#define HELLO "Hello from Info-ZIP 3.0.\n"
#define DEFLATE_LINE "Cabin consumes cabin-ports/minizip.\n"
#define SECRET_LINE "The password is not stored in the archive.\n"

enum { BIG = 200000 };
static char expected[BIG];
static char got[BIG + 1];
static char path[4096];

static int fail(const char* what) {
    fprintf(stderr, "minizip C test failed: %s\n", what);
    return 0;
}

static void repeat(const char* line, int times) {
    size_t n = strlen(line);
    int i;
    for (i = 0; i < times; ++i) memcpy(expected + i * n, line, n);
}

static int write_file(const unsigned char* data, size_t size) {
    FILE* f = fopen(path, "wb");
    int ok = f != NULL && fwrite(data, 1, size, f) == size;
    return (f != NULL && fclose(f) == 0) && ok;
}

/* Reads the current entry whole into `got`: its size, or -1 when opening or
 * inflating fails, the data ends before the entry's recorded size (a deflate
 * stream decrypted with a wrong password can end early), or the CRC check
 * at unzCloseCurrentFile, which runs only at the end, fails. */
static long read_entry(unzFile uf, const char* password) {
    long n = 0;
    int r;
    int at_end;
    int closed;
    if (unzOpenCurrentFilePassword(uf, password) != UNZ_OK) return -1;
    while ((r = unzReadCurrentFile(uf, got + n, (unsigned)(sizeof got - n))) > 0) n += r;
    at_end = unzeof(uf) == 1;
    closed = unzCloseCurrentFile(uf);
    return r == 0 && at_end && closed == UNZ_OK && n < (long)sizeof got ? n : -1;
}

static int entry_is(unzFile uf, const char* name, const char* password, unsigned long method,
                    unsigned long size, unsigned long crc) {
    unz_file_info64 fi;
    char found[64];
    return unzLocateFile(uf, name, 1) == UNZ_OK &&
           unzGetCurrentFileInfo64(uf, &fi, found, sizeof found, NULL, 0, NULL, 0) == UNZ_OK &&
           strcmp(found, name) == 0 && fi.compression_method == method &&
           fi.uncompressed_size == size && (crc == 0 || fi.crc == crc) &&
           read_entry(uf, password) == (long)size && memcmp(got, expected, size) == 0;
}

static int reads_the_reference_archive(void) {
    unz_global_info64 gi;
    unz_file_info64 fi;
    char comment[64];
    unzFile uf;
    int ok;
    if (!write_file(reference_zip, sizeof reference_zip)) return fail("writing the reference archive");
    uf = unzOpen64(path);
    if (uf == NULL) return fail("unzOpen64 on the reference archive");
    ok = unzGetGlobalInfo64(uf, &gi) == UNZ_OK && gi.number_entry == 3 &&
         gi.size_comment == strlen(REFERENCE_COMMENT) &&
         unzGetGlobalComment(uf, comment, sizeof comment) == (int)strlen(REFERENCE_COMMENT) &&
         strcmp(comment, REFERENCE_COMMENT) == 0;
    if (!ok) fail("reference archive directory");

    memcpy(expected, HELLO, strlen(HELLO));
    if (ok && !(entry_is(uf, "hello.txt", NULL, 0, strlen(HELLO), 0xd5a45a63UL) &&
                unzGetCurrentFileInfo64(uf, &fi, NULL, 0, NULL, 0, NULL, 0) == UNZ_OK &&
                fi.tmu_date.tm_year == 2026 && fi.tmu_date.tm_mon == 1 &&
                fi.tmu_date.tm_mday == 17 && fi.tmu_date.tm_hour == 12)) {
        ok = fail("stored entry");
    }
    repeat(DEFLATE_LINE, 32);
    if (ok && !entry_is(uf, "deflate.txt", NULL, Z_DEFLATED, 32 * strlen(DEFLATE_LINE), 0x8d0ffef5UL)) {
        ok = fail("deflated entry");
    }
    repeat(SECRET_LINE, 4);
    if (ok && !(entry_is(uf, "secret.txt", "cabin-ports", Z_DEFLATED, 4 * strlen(SECRET_LINE),
                         0x129e3b02UL) &&
                unzGetCurrentFileInfo64(uf, &fi, NULL, 0, NULL, 0, NULL, 0) == UNZ_OK &&
                (fi.flag & 1) != 0)) {
        ok = fail("encrypted entry");
    }
    if (ok && read_entry(uf, "cabin-port") != -1) ok = fail("wrong password accepted");
    if (ok && !(unzLocateFile(uf, "HELLO.TXT", 1) == UNZ_END_OF_LIST_OF_FILE &&
                unzLocateFile(uf, "HELLO.TXT", 2) == UNZ_OK)) {
        ok = fail("name lookup");
    }
    return unzClose(uf) == UNZ_OK && ok;
}

/* Byte 39 is the stored entry's first data byte. */
static int rejects_a_damaged_copy(void) {
    unsigned char damaged[sizeof reference_zip];
    unzFile uf;
    int status = UNZ_OK;
    memcpy(damaged, reference_zip, sizeof damaged);
    damaged[39] ^= 0x20;
    if (!write_file(damaged, sizeof damaged)) return fail("writing the damaged archive");
    uf = unzOpen64(path);
    if (uf == NULL) return fail("unzOpen64 on the damaged archive");
    if (unzLocateFile(uf, "hello.txt", 1) == UNZ_OK && unzOpenCurrentFile(uf) == UNZ_OK) {
        while (unzReadCurrentFile(uf, got, sizeof got) > 0) {
        }
        status = unzCloseCurrentFile(uf);
    }
    unzClose(uf);
    return status == UNZ_CRCERROR || fail("damaged entry passed its CRC check");
}

static int add(zipFile zf, const char* name, const char* comment, int method, const char* password,
               int zip64, const char* data, unsigned size) {
    zip_fileinfo zi;
    memset(&zi, 0, sizeof zi);
    zi.tmz_date.tm_year = 2026;
    zi.tmz_date.tm_mon = 10;
    zi.tmz_date.tm_mday = 6;
    zi.tmz_date.tm_hour = 9;
    zi.tmz_date.tm_min = 30;
    zi.tmz_date.tm_sec = 10;
    return zipOpenNewFileInZip3_64(zf, name, &zi, NULL, 0, NULL, 0, comment, method,
                                   Z_BEST_COMPRESSION, 0, -MAX_WBITS, DEF_MEM_LEVEL,
                                   Z_DEFAULT_STRATEGY, password,
                                   crc32(0L, (const Bytef*)data, size), zip64) == ZIP_OK &&
           zipWriteInFileInZip(zf, data, size) == ZIP_OK && zipCloseFileInZip(zf) == ZIP_OK;
}

static void fill_bytes(int n) {
    int i;
    for (i = 0; i < n; ++i) expected[i] = (char)(i * 7 + i / 251);
}

/* zip64 = 1 puts a Zip64 extended-information field (header 0x0001) in
 * the local header, even for an entry this small. */
static int has_local_zip64_field(unzFile uf) {
    unsigned char field[64];
    int n;
    if (unzOpenCurrentFile(uf) != UNZ_OK) return 0;
    n = unzGetLocalExtrafield(uf, field, sizeof field);
    unzCloseCurrentFile(uf);
    return n >= 4 && field[0] == 0x01 && field[1] == 0x00;
}

/* Entry `i` of the archive below, read back. */
static int entry_reads_back(unzFile uf, int i, const unz_file_info64* fi, const char* comment) {
    switch (i) {
    case 0:
        fill_bytes(1024);
        return fi->compression_method == 0 && strcmp(comment, "raw bytes") == 0 &&
               read_entry(uf, NULL) == 1024 && memcmp(got, expected, 1024) == 0;
    case 1:
        repeat(DEFLATE_LINE, BIG / (int)strlen(DEFLATE_LINE));
        return has_local_zip64_field(uf) && fi->compression_method == Z_DEFLATED &&
               fi->compressed_size < BIG / 20 && read_entry(uf, NULL) == BIG &&
               memcmp(got, expected, BIG) == 0;
    case 2:
        return (fi->flag & 1) != 0 && read_entry(uf, "s3cret") == (long)strlen(SECRET_LINE) &&
               memcmp(got, SECRET_LINE, strlen(SECRET_LINE)) == 0 && read_entry(uf, "secret") == -1;
    default:
        return read_entry(uf, NULL) == (long)strlen(HELLO) && memcmp(got, HELLO, strlen(HELLO)) == 0;
    }
}

static int writes_appends_and_reads_back(void) {
    static const char* const names[] = {"stored.bin", "dir/big.txt", "secret.txt", "appended.txt"};
    unz_global_info64 gi;
    unz_file_info64 fi;
    char name[64];
    char comment[64];
    unzFile uf;
    zipFile zf;
    int i;
    int ok;

    remove(path);
    zf = zipOpen64(path, APPEND_STATUS_CREATE);
    if (zf == NULL) return fail("zipOpen64 create");
    fill_bytes(1024);
    ok = add(zf, "stored.bin", "raw bytes", 0, NULL, 0, expected, 1024) &&
         zipAlreadyThere(zf, "stored.bin") == 1 && zipAlreadyThere(zf, "dir/big.txt") == 0;
    repeat(DEFLATE_LINE, BIG / (int)strlen(DEFLATE_LINE));
    ok = ok && add(zf, "dir/big.txt", NULL, Z_DEFLATED, NULL, 1, expected, BIG) &&
         add(zf, "secret.txt", NULL, Z_DEFLATED, "s3cret", 0, SECRET_LINE, strlen(SECRET_LINE));
    if (zipClose(zf, "written by the C consumer") != ZIP_OK || !ok) return fail("writing an archive");

    zf = zipOpen64(path, APPEND_STATUS_ADDINZIP);
    if (zf == NULL) return fail("zipOpen64 append");
    ok = zipAlreadyThere(zf, "dir/big.txt") == 1 && zipAlreadyThere(zf, "appended.txt") == 0 &&
         add(zf, "appended.txt", NULL, Z_DEFLATED, NULL, 0, HELLO, strlen(HELLO));
    if (zipClose(zf, NULL) != ZIP_OK || !ok) return fail("appending to an archive");

    uf = unzOpen64(path);
    if (uf == NULL) return fail("unzOpen64 on the written archive");
    ok = unzGetGlobalInfo64(uf, &gi) == UNZ_OK && gi.number_entry == 4 &&
         unzGetGlobalComment(uf, comment, sizeof comment) > 0 &&
         strcmp(comment, "written by the C consumer") == 0;
    for (i = 0; ok && i < 4; ++i) {
        ok = (i == 0 ? unzGoToFirstFile(uf) : unzGoToNextFile(uf)) == UNZ_OK &&
             unzGetCurrentFileInfo64(uf, &fi, name, sizeof name, NULL, 0, comment, sizeof comment) ==
                 UNZ_OK &&
             strcmp(name, names[i]) == 0 && fi.tmu_date.tm_year == 2026 &&
             fi.tmu_date.tm_mon == 10 && fi.tmu_date.tm_sec == 10 &&
             entry_reads_back(uf, i, &fi, comment);
    }
    ok = ok && unzGoToNextFile(uf) == UNZ_END_OF_LIST_OF_FILE;
    unzClose(uf);
    return ok || fail("reading the written archive back");
}

int main(void) {
    /* Among the probe's own build outputs: `cabin run` keeps the caller's
     * working directory. */
    const char* dir = getenv("CABIN_PROFILE_DIR");
    int ok;
    if (dir == NULL || strlen(dir) + 32 > sizeof path) {
        fail("CABIN_PROFILE_DIR");
        return 1;
    }
    snprintf(path, sizeof path, "%s/port-probe-minizip-c.zip", dir);
    ok = reads_the_reference_archive() && rejects_a_damaged_copy() && writes_appends_and_reads_back();
    remove(path);
    return ok ? 0 : 1;
}
