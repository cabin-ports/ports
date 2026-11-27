/* Builds a small UDF 1.02 image in memory (ECMA-167 volume recognition,
 * anchors at sector 256 and the last sector, main and reserve volume
 * descriptor sequences, a file set, an inline root directory and one file
 * recorded in two blocks) and reads it through a custom block input and
 * through udfread_open() on a copy written to disk. */
#include <udfread/blockinput.h>
#include <udfread/udfread-version.h>
#include <udfread/udfread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 2048
#define PART_START 257 /* partition: FSD, TD, root FE, file FE, 2 data blocks */
#define PART_LEN 6
#define SECTORS (PART_START + PART_LEN + 1)
#define FILE_SIZE 2500

static int failures;

static int check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
    return ok;
}

static void put16(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, unsigned long v) {
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

static unsigned char *sector(unsigned char *img, unsigned long lba) {
    return img + lba * BLOCK;
}

/* CRC-ITU-T of ECMA-167 3/7.2.6. */
static unsigned long crc16(const unsigned char *p, size_t n) {
    unsigned long crc = 0;
    int i;
    while (n--) {
        crc ^= (unsigned long)*p++ << 8;
        for (i = 0; i < 8; i++)
            crc = (crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1) & 0xffff;
    }
    return crc;
}

/* Descriptor tag (3/7.2) of a filled-in descriptor of len bytes. */
static void tag(unsigned char *d, unsigned id, unsigned long loc, size_t len) {
    unsigned sum = 0;
    int i;
    put16(d, id);
    put16(d + 2, 2);
    put16(d + 8, crc16(d + 16, len - 16));
    put16(d + 10, (unsigned long)(len - 16));
    put32(d + 12, loc);
    for (i = 0; i < 16; i++)
        if (i != 4) sum += d[i];
    d[4] = (unsigned char)sum;
}

static void entity(unsigned char *p, const char *id) {
    memcpy(p + 1, id, strlen(id));
}

static void charspec(unsigned char *p) {
    memcpy(p + 1, "OSTA Compressed Unicode", 23);
}

/* 8-bit CS0 dstring of a field_len-byte field. */
static void dstring(unsigned char *p, size_t field_len, const char *s) {
    size_t n = strlen(s);
    p[0] = 8;
    memcpy(p + 1, s, n);
    p[field_len - 1] = (unsigned char)(n + 1);
}

static void long_ad(unsigned char *p, unsigned long len, unsigned long lba) {
    put32(p, len);
    put32(p + 4, lba);
    put16(p + 8, 0);
}

static const char volume_id[] = "Caf\xe9 UDF"; /* Latin-1 e-acute */

static void volume_descriptors(unsigned char *img, unsigned long base) {
    unsigned char *d = sector(img, base); /* Primary Volume Descriptor */
    put32(d + 16, 1);
    dstring(d + 24, 32, volume_id);
    put16(d + 56, 1);
    put16(d + 58, 1);
    put16(d + 60, 2);
    put16(d + 62, 2);
    put32(d + 64, 1);
    put32(d + 68, 1);
    dstring(d + 72, 128, "0123456789ABCDEFcabin-ports");
    charspec(d + 200);
    charspec(d + 264);
    tag(d, 1, base, 512);

    d = sector(img, base + 1); /* Partition Descriptor, partition 0 */
    put32(d + 16, 2);
    put16(d + 20, 1);
    entity(d + 24, "+NSR02");
    put32(d + 184, 1);
    put32(d + 188, PART_START);
    put32(d + 192, PART_LEN);
    tag(d, 5, base + 1, 512);

    d = sector(img, base + 2); /* Logical Volume Descriptor */
    put32(d + 16, 3);
    charspec(d + 20);
    dstring(d + 84, 128, "cabin");
    put32(d + 212, BLOCK);
    entity(d + 216, "*OSTA UDF Compliant");
    put16(d + 240, 0x0102);
    long_ad(d + 248, BLOCK, 0); /* File Set Descriptor */
    put32(d + 264, 6);
    put32(d + 268, 1);
    d[440] = 1; /* type 1 partition map onto partition 0 */
    d[441] = 6;
    put16(d + 442, 1);
    tag(d, 6, base + 2, 446);

    tag(sector(img, base + 3), 8, base + 3, 512); /* Terminating */
}

static void anchor(unsigned char *img, unsigned long lba) {
    unsigned char *d = sector(img, lba);
    put32(d + 16, 4 * BLOCK);
    put32(d + 20, 32);
    put32(d + 24, 4 * BLOCK);
    put32(d + 28, 48);
    tag(d, 2, lba, 512);
}

static unsigned char *file_entry(unsigned char *img, unsigned long block, unsigned type,
                                 unsigned ad_flags, unsigned long size, unsigned long l_ad) {
    unsigned char *d = sector(img, PART_START + block);
    put16(d + 20, 4); /* ICB strategy 4 */
    put16(d + 24, 1);
    d[27] = (unsigned char)type;
    put16(d + 34, ad_flags);
    put16(d + 48, 1);
    put32(d + 56, size);
    put32(d + 64, ad_flags == 3 ? 0 : (size + BLOCK - 1) / BLOCK);
    entity(d + 128, "*cabin-ports");
    put32(d + 160, block);
    put32(d + 172, l_ad);
    return d;
}

static size_t fid(unsigned char *p, unsigned long block, unsigned flags, const char *name,
                  unsigned long icb) {
    size_t l_fi = name ? strlen(name) + 1 : 0, len = (38 + l_fi + 3) / 4 * 4;
    put16(p + 16, 1);
    p[18] = (unsigned char)flags;
    p[19] = (unsigned char)l_fi;
    long_ad(p + 20, BLOCK, icb);
    if (name) {
        p[38] = 8; /* 8-bit CS0 */
        memcpy(p + 39, name, l_fi - 1);
    }
    tag(p, 257, block, len);
    return len;
}

static unsigned char content(size_t i) {
    return (unsigned char)(i % 251);
}

static unsigned char *build_image(void) {
    static const char *const vrs[] = {"BEA01", "NSR02", "TEA01"};
    unsigned char *img = (unsigned char *)calloc(SECTORS, BLOCK), *d;
    size_t i, dir_len;
    if (!img) return NULL;
    for (i = 0; i < 3; i++) {
        d = sector(img, 16 + i);
        memcpy(d + 1, vrs[i], 5);
        d[6] = 1;
    }
    volume_descriptors(img, 32);
    volume_descriptors(img, 48);
    anchor(img, 256);
    anchor(img, SECTORS - 1);

    d = sector(img, PART_START); /* File Set Descriptor, block 0 */
    put16(d + 28, 3);
    put16(d + 30, 3);
    put32(d + 32, 1);
    put32(d + 36, 1);
    charspec(d + 48);
    dstring(d + 112, 128, "cabin");
    charspec(d + 240);
    dstring(d + 304, 32, "cabin");
    long_ad(d + 400, BLOCK, 2); /* root directory ICB */
    entity(d + 416, "*OSTA UDF Compliant");
    put16(d + 440, 0x0102);
    tag(d, 256, 0, 512);
    tag(sector(img, PART_START + 1), 8, 1, 512);

    /* Root directory: parent entry and hello.txt, inline in its File Entry. */
    d = sector(img, PART_START + 2);
    dir_len = fid(d + 176, 2, 0x0a, NULL, 2);
    dir_len += fid(d + 176 + dir_len, 2, 0, "hello.txt", 3);
    file_entry(img, 2, 4, 3, (unsigned long)dir_len, (unsigned long)dir_len);
    tag(d, 261, 2, 176 + dir_len);

    /* hello.txt: one short_ad extent over blocks 4 and 5. */
    d = file_entry(img, 3, 5, 0, FILE_SIZE, 8);
    put32(d + 176, FILE_SIZE);
    put32(d + 180, 4);
    tag(d, 261, 3, 184);
    for (i = 0; i < FILE_SIZE; i++) sector(img, PART_START + 4)[i] = content(i);
    return img;
}

struct memory_input {
    udfread_block_input input; /* first, so the callbacks can cast back */
    const unsigned char *img;
    uint32_t blocks;
    int closed;
};

static int mem_read(udfread_block_input *in, uint32_t lba, void *buf, uint32_t nblocks, int flags) {
    struct memory_input *m = (struct memory_input *)in;
    (void)flags;
    if (lba >= m->blocks) return -1;
    if (nblocks > m->blocks - lba) nblocks = m->blocks - lba;
    memcpy(buf, m->img + (size_t)lba * BLOCK, (size_t)nblocks * BLOCK);
    return (int)nblocks;
}

static uint32_t mem_size(udfread_block_input *in) {
    return ((struct memory_input *)in)->blocks;
}

static int mem_close(udfread_block_input *in) {
    ((struct memory_input *)in)->closed++;
    return 0;
}

static int bytes_match(const unsigned char *buf, size_t from, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (buf[i] != content(from + i)) return 0;
    return 1;
}

static void read_volume(udfread *udf, const unsigned char *img) {
    struct udfread_dirent entry;
    unsigned char set_id[128], buf[4096];
    UDFDIR *dir;
    UDFFILE *f;
    const char *id = udfread_get_volume_id(udf);

    check(id != NULL && strcmp(id, "Caf\xc3\xa9 UDF") == 0, "volume id in MUTF-8");
    check(udfread_get_volume_set_id(udf, set_id, sizeof set_id) == 128, "volume set id size");
    check(memcmp(set_id, img + 32 * BLOCK + 72, 128) == 0, "volume set id bytes");

    dir = udfread_opendir(udf, "/");
    if (check(dir != NULL, "opendir /")) {
        check(udfread_readdir(dir, &entry) == &entry && strcmp(entry.d_name, "hello.txt") == 0 &&
                  entry.d_type == UDF_DT_REG,
              "readdir: hello.txt, a regular file");
        check(udfread_readdir(dir, &entry) == NULL, "readdir: one entry");
        udfread_rewinddir(dir);
        check(udfread_readdir(dir, &entry) != NULL && strcmp(entry.d_name, "hello.txt") == 0,
              "rewinddir");
        f = udfread_file_openat(dir, "hello.txt");
        check(f != NULL && udfread_file_size(f) == FILE_SIZE, "file_openat");
        udfread_file_close(f);
        udfread_closedir(dir);
    }

    check(udfread_file_open(udf, "/missing.txt") == NULL, "missing file");
    f = udfread_file_open(udf, "/hello.txt");
    if (!check(f != NULL, "file_open /hello.txt")) return;
    check(udfread_file_size(f) == FILE_SIZE, "file size");
    check(udfread_file_lba(f, 0) == PART_START + 4 && udfread_file_lba(f, 1) == PART_START + 5,
          "file blocks map to the partition");
    check(udfread_read_blocks(f, buf, 0, 2, 0) == 2 && bytes_match(buf, 0, FILE_SIZE),
          "read_blocks");
    check(udfread_file_read(f, buf, sizeof buf) == FILE_SIZE && bytes_match(buf, 0, FILE_SIZE),
          "file_read: the whole file");
    check(udfread_file_tell(f) == FILE_SIZE && udfread_file_read(f, buf, 1) == 0, "file_read: EOF");
    check(udfread_file_seek(f, 2040, UDF_SEEK_SET) == 2040 && udfread_file_read(f, buf, 16) == 16 &&
              bytes_match(buf, 2040, 16),
          "seek and read across the block boundary");
    check(udfread_file_seek(f, -4, UDF_SEEK_END) == FILE_SIZE - 4 &&
              udfread_file_read(f, buf, 10) == 4 && bytes_match(buf, FILE_SIZE - 4, 4),
          "seek from the end and a short read");
    udfread_file_close(f);
}

static void from_memory(const unsigned char *img) {
    struct memory_input m;
    udfread *udf = udfread_init();
    if (!check(udf != NULL, "udfread_init")) return;
    m.input.close = mem_close;
    m.input.read = mem_read;
    m.input.size = mem_size;
    m.img = img;
    m.blocks = SECTORS;
    m.closed = 0;
    if (check(udfread_open_input(udf, &m.input) == 0, "open_input")) read_volume(udf, img);
    udfread_close(udf);
    check(m.closed == 1, "close calls the input's close once");
}

/* `cabin run` keeps the caller's working directory, so the file goes in the
   probe's output directory. */
static void from_file(const unsigned char *img) {
    const char *dir = getenv("CABIN_PROFILE_DIR");
    char path[1024];
    udfread *udf;
    FILE *out;

    if (!check(dir != NULL, "CABIN_PROFILE_DIR is set")) return;
    snprintf(path, sizeof path, "%s/port-probe-libudfread-c.iso", dir);
    out = fopen(path, "wb");
    if (!check(out != NULL, "create the image file")) return;
    check(fwrite(img, BLOCK, SECTORS, out) == SECTORS, "write the image file");
    fclose(out);
    udf = udfread_init();
    if (check(udf != NULL && udfread_open(udf, path) == 0, "udfread_open on the image file"))
        read_volume(udf, img);
    udfread_close(udf);
    remove(path);
}

static void not_udf(void) {
    struct memory_input m;
    unsigned char *zeros = (unsigned char *)calloc(SECTORS, BLOCK);
    udfread *udf = udfread_init();
    if (zeros && udf) {
        m.input.close = mem_close;
        m.input.read = mem_read;
        m.input.size = mem_size;
        m.img = zeros;
        m.blocks = SECTORS;
        m.closed = 0;
        check(udfread_open_input(udf, &m.input) < 0, "an all-zero image is refused");
    }
    udfread_close(udf);
    free(zeros);
}

int main(void) {
    int major = -1, minor = -1, micro = -1;
    unsigned char *img = build_image();

    udfread_get_version(&major, &minor, &micro);
    check(major == 1 && minor == 2 && micro == 0, "udfread_get_version");
    check(UDFREAD_VERSION == UDFREAD_VERSION_CODE(1, 2, 0) &&
              strcmp(UDFREAD_VERSION_STRING, "1.2.0") == 0,
          "version macros");

    if (check(img != NULL, "build the image")) {
        from_memory(img);
        from_file(img);
    }
    not_udf();
    free(img);
    if (failures) return 1;
    puts("libudfread C consumer: OK");
    return 0;
}
