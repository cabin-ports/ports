/* QR codes built by an independent encoder (a short Python script following
 * ISO/IEC 18004, checked against the codewords of its Annex I worked example)
 * decode from a cell bitmap, through error correction, and from a rendered
 * image, plain and mirrored; grid-size and error-correction failures are
 * reported.  C99. */
#include <quirc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, int line, const char *what)
{
    if (!ok) {
        fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

#define N 21

/* "01234567" in numeric mode, version 1-M, mask 2: ISO/IEC 18004 Annex I.
 * Data codewords 10 20 0c 56 61 80 ec 11 ec 11 ..., EC a5 24 d4 c1 ed 36 c7
 * 87 2c 55. */
static const char *const annex_i[N] = {
    "#######..#.##.#######",
    "#.....#..####.#.....#",
    "#.###.#.#.....#.###.#",
    "#.###.#.##....#.###.#",
    "#.###.#.#.###.#.###.#",
    "#.....#.#...#.#.....#",
    "#######.#.#.#.#######",
    "........#..##........",
    "#.#####..#..#.#####..",
    "...#.#.##.#.#..#.##..",
    "..#...##.#.#.#..#####",
    "....#....#.....####..",
    "...######..#.#..#....",
    "........#.#####..##..",
    "#######..##.#.##.....",
    "#.....#.#.#####...#.#",
    "#.###.#.#...#..#.##..",
    "#.###.#.##..#..#.....",
    "#.###.#.#.##.#..#.#..",
    "#.....#........##.##.",
    "#######.####.#..#.#..",
};

/* "Cabin quirc 1.2" in byte mode, version 1-L, mask 5. */
static const char *const byte_l5[N] = {
    "#######....##.#######",
    "#.....#...#.#.#.....#",
    "#.###.#...#...#.###.#",
    "#.###.#.#.#.#.#.###.#",
    "#.###.#.##.#..#.###.#",
    "#.....#..#..#.#.....#",
    "#######.#.#.#.#######",
    ".....................",
    "##...###...#....##...",
    "#...#..##....###..#..",
    ".###.##...#.##...###.",
    "#..###....####.#.####",
    "###...#.##.###.###.##",
    "........##.#..###.#..",
    "#######.#...##.....#.",
    "#.....#.#.#.###.####.",
    "#.###.#..#..####...##",
    "#.###.#.........#....",
    "#.###.#....###.##.###",
    "#.....#.####.#..###..",
    "#######.#...#.#.#..#.",
};

static void set_cell(struct quirc_code *code, int x, int y, int dark)
{
    int i = y * code->size + x;
    if (dark)
        code->cell_bitmap[i >> 3] |= (uint8_t)(1u << (i & 7));
    else
        code->cell_bitmap[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

static void to_code(const char *const rows[N], struct quirc_code *code)
{
    int x, y;
    memset(code, 0, sizeof *code);
    code->size = N;
    for (y = 0; y < N; ++y)
        for (x = 0; x < N; ++x)
            set_cell(code, x, y, rows[y][x] == '#');
}

static void flip_cell(struct quirc_code *code, const char *const rows[N],
                      int x, int y)
{
    set_cell(code, x, y, rows[y][x] != '#');
}

static struct quirc_code code;
static struct quirc_data data;

static void test_bitmap(void)
{
    int y;

    to_code(annex_i, &code);
    CHECK(quirc_decode(&code, &data) == QUIRC_SUCCESS);
    CHECK(data.version == 1);
    CHECK(data.ecc_level == QUIRC_ECC_LEVEL_M);
    CHECK(data.mask == 2);
    CHECK(data.data_type == QUIRC_DATA_TYPE_NUMERIC);
    CHECK(data.payload_len == 8);
    CHECK(strcmp((const char *)data.payload, "01234567") == 0);

    /* The data starts at the bottom right: rows 20-17, 16-13, and 12-9 of
     * columns 19 and 20 hold codewords 0, 1, and 2.  1-M corrects them. */
    flip_cell(&code, annex_i, 20, 20);
    flip_cell(&code, annex_i, 19, 15);
    flip_cell(&code, annex_i, 20, 10);
    memset(&data, 0, sizeof data);
    CHECK(quirc_decode(&code, &data) == QUIRC_SUCCESS);
    CHECK(strcmp((const char *)data.payload, "01234567") == 0);

    /* Errors in codewords 0-5 and 14, seven in all, are more than 1-M's 10
     * EC codewords can correct. */
    for (y = 9; y <= 20; y += 4)
        flip_cell(&code, annex_i, 18, y);
    flip_cell(&code, annex_i, 11, 9);
    CHECK(quirc_decode(&code, &data) == QUIRC_ERROR_DATA_ECC);

    to_code(annex_i, &code);
    code.size = 22;
    CHECK(quirc_decode(&code, &data) == QUIRC_ERROR_INVALID_GRID_SIZE);

    CHECK(strcmp(quirc_strerror(QUIRC_SUCCESS), "Success") == 0);
    CHECK(strcmp(quirc_strerror(QUIRC_ERROR_DATA_ECC), "ECC failure") == 0);
}

#define SCALE 4
#define QUIET 4
#define SIDE ((N + 2 * QUIET) * SCALE)

/* Renders rows (transposed if mirror) black on white with a 4-module quiet
 * zone, and returns the number of codes quirc finds. */
static int scan(struct quirc *q, const char *const rows[N], int mirror)
{
    int w = 0, h = 0, x, y;
    uint8_t *image = quirc_begin(q, &w, &h);

    CHECK(w == SIDE && h == SIDE);
    memset(image, 255, (size_t)w * h);
    for (y = 0; y < N; ++y)
        for (x = 0; x < N; ++x) {
            char cell = mirror ? rows[x][y] : rows[y][x];
            int px, py;
            if (cell != '#')
                continue;
            for (py = 0; py < SCALE; ++py)
                for (px = 0; px < SCALE; ++px)
                    image[((QUIET + y) * SCALE + py) * w +
                          (QUIET + x) * SCALE + px] = 0;
        }
    quirc_end(q);
    return quirc_count(q);
}

static int near(int a, int b)
{
    return abs(a - b) <= 2;
}

static void test_image(void)
{
    struct quirc *q = quirc_new();
    int w = 0, h = 0, resized;
    const int lo = QUIET * SCALE, hi = (QUIET + N) * SCALE;

    CHECK(q != NULL);
    if (!q)
        return;
    resized = quirc_resize(q, SIDE, SIDE) == 0;
    CHECK(resized);
    if (!resized) {
        quirc_destroy(q);
        return;
    }

    quirc_begin(q, &w, &h);
    memset(quirc_begin(q, NULL, NULL), 255, (size_t)w * h);
    quirc_end(q);
    CHECK(quirc_count(q) == 0);

    CHECK(scan(q, byte_l5, 0) == 1);
    memset(&code, 0, sizeof code);
    quirc_extract(q, 0, &code);
    CHECK(code.size == N);
    /* Corners from the top left, clockwise. */
    CHECK(near(code.corners[0].x, lo) && near(code.corners[0].y, lo));
    CHECK(near(code.corners[1].x, hi) && near(code.corners[1].y, lo));
    CHECK(near(code.corners[2].x, hi) && near(code.corners[2].y, hi));
    CHECK(near(code.corners[3].x, lo) && near(code.corners[3].y, hi));
    memset(&data, 0, sizeof data);
    CHECK(quirc_decode(&code, &data) == QUIRC_SUCCESS);
    CHECK(data.version == 1);
    CHECK(data.ecc_level == QUIRC_ECC_LEVEL_L);
    CHECK(data.mask == 5);
    CHECK(data.data_type == QUIRC_DATA_TYPE_BYTE);
    CHECK(data.payload_len == 15);
    CHECK(strcmp((const char *)data.payload, "Cabin quirc 1.2") == 0);

    /* A mirrored code (ISO/IEC 18004:2015) decodes once quirc_flip
     * transposes its cells back. */
    CHECK(scan(q, byte_l5, 1) == 1);
    quirc_extract(q, 0, &code);
    CHECK(quirc_decode(&code, &data) != QUIRC_SUCCESS);
    quirc_flip(&code);
    memset(&data, 0, sizeof data);
    CHECK(quirc_decode(&code, &data) == QUIRC_SUCCESS);
    CHECK(strcmp((const char *)data.payload, "Cabin quirc 1.2") == 0);

    quirc_destroy(q);
}

int main(void)
{
    /* Upstream's version string was not updated for 1.2. */
    CHECK(strcmp(quirc_version(), "1.0") == 0);
    test_bitmap();
    test_image();
    if (failures != 0)
        return 1;
    puts("quirc 1.2.0: C API OK");
    return 0;
}
