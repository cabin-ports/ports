/* The C++ consumer: builds a tree with the sequential-write API, edits it in
 * place, and reads it back through the read-only API. */
#include <libfdt.h>

#include <stdint.h>
#include <cstdio>
#include <cstring>

enum { SIZE = 1024, BIG = 2048 };
static uint64_t blob[SIZE / 8];
static uint64_t big[BIG / 8];

static int failures;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "dtc C++ test failed: line %d: %s\n", __LINE__, #cond); \
            failures++;                                                  \
        }                                                                \
    } while (0)

static int build(void) {
    return fdt_create(blob, SIZE) ||
           fdt_finish_reservemap(blob) ||
           fdt_begin_node(blob, "") ||
           fdt_property_u32(blob, "#address-cells", 1) ||
           fdt_property_string(blob, "compatible", "acme,board") ||
           fdt_begin_node(blob, "cpus") ||
           fdt_property_u64(blob, "freq", 0x0123456789abcdefULL) ||
           fdt_end_node(blob) ||
           fdt_end_node(blob) ||
           fdt_finish(blob);
}

int main(void) {
    int off, soc, len;
    const fdt32_t* cells;
    const fdt64_t* freq;
    const char* s;

    CHECK(build() == 0);
    CHECK(fdt_check_header(blob) == 0);
    CHECK(fdt_magic(blob) == FDT_MAGIC);
    CHECK(fdt_totalsize(blob) <= SIZE);

    CHECK(fdt_open_into(blob, big, BIG) == 0);
    CHECK(fdt_totalsize(big) == BIG);
    CHECK(fdt_setprop_string(big, 0, "model", "Acme One") == 0);
    CHECK(fdt_setprop_u32(big, 0, "#address-cells", 2) == 0);
    soc = fdt_add_subnode(big, 0, "soc");
    CHECK(soc > 0 && soc == fdt_path_offset(big, "/soc"));
    CHECK(fdt_check_full(big, BIG) == 0);
    CHECK(fdt_create_empty_tree(blob, SIZE) == 0 && fdt_path_offset(blob, "/") == 0);
    CHECK(fdt_check_header(big) == 0);

    cells = (const fdt32_t*)fdt_getprop(big, 0, "#address-cells", &len);
    CHECK(cells != NULL && len == 4 && fdt32_to_cpu(*cells) == 2);
    s = (const char*)fdt_getprop(big, 0, "compatible", &len);
    CHECK(s != NULL && len == 11 && strcmp(s, "acme,board") == 0);
    s = (const char*)fdt_getprop(big, 0, "model", &len);
    CHECK(s != NULL && len == 9 && strcmp(s, "Acme One") == 0);
    CHECK(fdt_node_check_compatible(big, 0, "acme,board") == 0);
    CHECK(fdt_node_check_compatible(big, 0, "acme,other") == 1);

    off = fdt_path_offset(big, "/cpus");
    CHECK(off > 0);
    CHECK(strcmp(fdt_get_name(big, off, &len), "cpus") == 0 && len == 4);
    freq = (const fdt64_t*)fdt_getprop(big, off, "freq", &len);
    CHECK(freq != NULL && len == 8 && fdt64_ld(freq) == 0x0123456789abcdefULL);
    CHECK(fdt_getprop(big, off, "absent", &len) == NULL && len == -FDT_ERR_NOTFOUND);
    CHECK(fdt_path_offset(big, "/soc") > 0);
    CHECK(fdt_path_offset(big, "/missing") == -FDT_ERR_NOTFOUND);
    CHECK(fdt_path_offset(big, "cpus") == -FDT_ERR_BADPATH);
    CHECK(fdt_subnode_offset(big, 0, "cpus") == off);

    CHECK(fdt_delprop(big, 0, "model") == 0);
    CHECK(fdt_getprop(big, 0, "model", &len) == NULL && len == -FDT_ERR_NOTFOUND);
    off = fdt_path_offset(big, "/cpus"); /* offsets shift after an edit */
    CHECK(fdt_del_node(big, off) == 0);
    CHECK(fdt_path_offset(big, "/cpus") == -FDT_ERR_NOTFOUND);

    CHECK(fdt_open_into(blob, big, 8) == -FDT_ERR_NOSPACE);
    *reinterpret_cast<char*>(big) ^= 1;
    CHECK(fdt_check_header(big) == -FDT_ERR_BADMAGIC);

    CHECK(strcmp(fdt_strerror(-FDT_ERR_NOTFOUND), "FDT_ERR_NOTFOUND") == 0);

    if (failures) return 1;
    puts("dtc C++ test passed");
    return 0;
}
