/* chunkio: the CRC-32 check value; a memory chunk whose transaction is
 * rolled back; then filesystem streams with checksums on: write a chunk's
 * metadata and content and a chunk that grows far past its first mapping,
 * close the context, rescan it and read both back, then corrupt one byte on
 * disk and see the checksum reject that chunk.  The CRC values come from
 * Python's zlib.crc32. */
#include <chunkio/chunkio.h>
#include <chunkio/cio_crc32.h>
#include <chunkio/cio_error.h>
#include <chunkio/cio_meta.h>
#include <chunkio/cio_utils.h>
#include <chunkio/cio_version.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BIG_SIZE 100000

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* The chunk's content equals the expected bytes. */
static int content_is(struct cio_chunk *ch, const char *want, size_t len)
{
    void *buf = NULL;
    size_t size = 0;
    int ok;

    ok = cio_chunk_get_content_copy(ch, &buf, &size) == CIO_OK &&
         size == len && memcmp(buf, want, len) == 0;
    free(buf);
    return ok;
}

/* The 4 CRC bytes in the chunk header, stored big-endian. */
static int hash_is(struct cio_chunk *ch, unsigned long want)
{
    const unsigned char *p = (const unsigned char *) cio_chunk_hash(ch);

    return p[0] == ((want >> 24) & 0xff) && p[1] == ((want >> 16) & 0xff) &&
           p[2] == ((want >> 8) & 0xff) && p[3] == (want & 0xff);
}

static struct cio_ctx *open_root(const char *root)
{
    struct cio_options opts;
    struct cio_ctx *ctx;

    cio_options_init(&opts);
    opts.root_path = (char *) root;
    opts.flags = CIO_OPEN | CIO_CHECKSUM;
    ctx = cio_create(&opts);
    if (ctx != NULL && cio_load(ctx, NULL) != 0) {
        cio_destroy(ctx);
        ctx = NULL;
    }
    return ctx;
}

static struct cio_chunk *only_chunk(struct cio_ctx *ctx, const char *stream)
{
    struct cio_stream *st = cio_stream_get(ctx, stream);

    if (st == NULL || mk_list_size(&st->chunks) != 1) {
        return NULL;
    }
    return mk_list_entry_first(&st->chunks, struct cio_chunk, _head);
}

static void memory_chunk(void)
{
    struct cio_ctx *ctx;
    struct cio_stream *st;
    struct cio_chunk *ch;
    int err = CIO_ERROR;

    ctx = cio_create(NULL);
    check(ctx != NULL, "cio_create(NULL)");
    if (ctx == NULL) {
        return;
    }
    st = cio_stream_create(ctx, "mem", CIO_STORE_MEM);
    ch = st ? cio_chunk_open(ctx, st, "m1", CIO_OPEN, 64, &err) : NULL;
    check(ch != NULL && err == CIO_OK, "memory chunk");
    if (ch != NULL) {
        check(cio_chunk_write(ch, "abc", 3) == 0, "memory write");
        check(cio_chunk_tx_begin(ch) == CIO_OK, "tx begin");
        check(cio_chunk_write(ch, "def", 3) == 0, "write in tx");
        check(content_is(ch, "abcdef", 6), "content in tx");
        check(cio_chunk_tx_rollback(ch) == CIO_OK, "tx rollback");
        check(cio_chunk_get_content_size(ch) == 3 && content_is(ch, "abc", 3),
              "content after rollback");
    }
    cio_destroy(ctx);
}

int main(void)
{
    const char *tmp;
    char root[512];
    char path[600];
    static char big[BIG_SIZE];
    struct cio_ctx *ctx;
    struct cio_stream *st;
    struct cio_stream *bst;
    struct cio_chunk *ch;
    char *meta;
    int meta_len;
    int err = CIO_ERROR;
    int i;
    FILE *f;
    crc_t crc;

    crc = cio_crc32_update(cio_crc32_init(), "123456789", 9);
    check(cio_crc32_finalize(crc) == 0xCBF43926u, "CRC-32 check value");

    check(strcmp(cio_version(), "1.5.4") == 0, "cio_version");
    check(CIO_VERSION_MAJOR == 1 && CIO_VERSION_MINOR == 5 &&
          CIO_VERSION_PATCH == 4, "CIO_VERSION_* macros");

    memory_chunk();

    for (i = 0; i < BIG_SIZE; i++) {
        big[i] = (char) ('a' + i % 26);
    }
    tmp = getenv("TMPDIR");
    if (tmp == NULL || *tmp == '\0') {
        tmp = getenv("TEMP");
    }
    if (tmp == NULL || *tmp == '\0') {
        tmp = "/tmp";
    }
    snprintf(root, sizeof(root), "%s/chunkio-test-%lu", tmp,
             (unsigned long) time(NULL));
    snprintf(path, sizeof(path), "%s/logs/c1.flb", root);

    ctx = open_root(root);
    check(ctx != NULL, "cio_create");
    if (ctx == NULL) {
        return 1;
    }
    st = cio_stream_create(ctx, "logs", CIO_STORE_FS);
    bst = cio_stream_create(ctx, "big", CIO_STORE_FS);
    check(st != NULL && bst != NULL, "cio_stream_create");
    if (st == NULL || bst == NULL) {
        cio_destroy(ctx);
        return 1;
    }

    /* A 2-byte metadata length, "tag-1", then two appends. */
    ch = cio_chunk_open(ctx, st, "c1.flb", CIO_OPEN, 1000, &err);
    check(ch != NULL && err == CIO_OK, "cio_chunk_open");
    if (ch != NULL) {
        check(cio_meta_write(ch, "tag-1", 5) == 0, "cio_meta_write");
        check(cio_chunk_write(ch, "hello, ", 7) == 0, "first write");
        check(cio_chunk_write(ch, "chunkio", 7) == 0, "second write");
        check(cio_chunk_sync(ch) == 0, "cio_chunk_sync");
        check(cio_chunk_get_content_size(ch) == 14, "content size");
        /* zlib.crc32(b"\0\5tag-1hello, chunkio") */
        check(hash_is(ch, 0x34fc82b7ul), "stored CRC-32");
    }

    /* Ten 10000-byte appends into a 1000-byte chunk. */
    ch = cio_chunk_open(ctx, bst, "b1.flb", CIO_OPEN, 1000, &err);
    check(ch != NULL && err == CIO_OK, "open big chunk");
    if (ch != NULL) {
        for (i = 0; i < BIG_SIZE; i += 10000) {
            check(cio_chunk_write(ch, big + i, 10000) == 0, "big write");
        }
        check(cio_chunk_sync(ch) == 0, "big sync");
        check(cio_chunk_get_content_size(ch) == BIG_SIZE, "big content size");
        /* zlib.crc32(b"\0\0" + big) */
        check(hash_is(ch, 0xbfa866adul), "big stored CRC-32");
    }
    cio_destroy(ctx);

    /* Rescan: both streams and chunks come back and verify. */
    ctx = open_root(root);
    check(ctx != NULL, "reopen");
    if (ctx == NULL) {
        return 1;
    }
    ch = only_chunk(ctx, "logs");
    check(ch != NULL && strcmp(ch->name, "c1.flb") == 0, "scanned chunk");
    if (ch != NULL) {
        check(cio_meta_read(ch, &meta, &meta_len) == 0 && meta_len == 5 &&
              memcmp(meta, "tag-1", 5) == 0, "metadata read back");
        check(content_is(ch, "hello, chunkio", 14), "content read back");
    }
    ch = only_chunk(ctx, "big");
    check(ch != NULL && strcmp(ch->name, "b1.flb") == 0, "scanned big chunk");
    if (ch != NULL) {
        check(content_is(ch, big, BIG_SIZE), "big content read back");
    }
    cio_destroy(ctx);

    /* Corrupt the last content byte of c1.flb (24-byte header, 5 of
     * metadata): the scan skips it, and opening it reports the checksum. */
    f = fopen(path, "r+b");
    check(f != NULL, "open chunk file");
    if (f != NULL) {
        check(fseek(f, 24 + 5 + 13, SEEK_SET) == 0 && fputc('O', f) == 'O',
              "corrupt chunk file");
        fclose(f);
    }
    ctx = open_root(root);
    check(ctx != NULL, "reopen corrupted");
    if (ctx != NULL) {
        st = cio_stream_get(ctx, "logs");
        check(st != NULL && mk_list_size(&st->chunks) == 0,
              "corrupted chunk not loaded");
        check(only_chunk(ctx, "big") != NULL, "big chunk still loaded");
        if (st != NULL) {
            ch = cio_chunk_open(ctx, st, "c1.flb", CIO_OPEN, 0, &err);
            check(ch == NULL && err == CIO_CORRUPTED &&
                  ctx->last_chunk_error == CIO_ERR_BAD_CHECKSUM,
                  "bad checksum");
        }
        cio_destroy(ctx);
    }

    check(cio_utils_recursive_delete(root) == 0, "recursive delete");
    f = fopen(path, "rb");
    check(f == NULL, "chunk file deleted");
    if (f != NULL) {
        fclose(f);
    }

    if (failures == 0) {
        printf("chunkio: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
