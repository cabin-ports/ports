/* Writes (from C++) a point shapefile, a polygon shapefile with its quadtree query and a
   DBF through SHPCreateLL and DBFCreateLL with custom hooks, reopens them and
   checks the stored geometry, bounds, counts and attributes. */
#include <shapefil.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int ok = 1;
static int opens = 0;
static int errors = 0;
static SAHooks hooks;
static SAHooks defaults;

static void check(int pass, const char *what)
{
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

static SAFile counting_open(const char *name, const char *access, void *user)
{
    opens++;
    return defaults.FOpen(name, access, user);
}

static void counting_error(const char *message)
{
    (void)message;
    errors++;
}

static void make_path(char *out, std::size_t size, const char *name)
{
    const char *dir = std::getenv("TMPDIR");
    if (!dir || !*dir)
        dir = std::getenv("TEMP");
    if (!dir || !*dir)
        dir = std::getenv("TMP");
    if (!dir || !*dir)
        dir = ".";
    if (std::strlen(dir) + std::strlen(name) + 2 > size)
        std::abort();
    std::strcpy(out, dir);
    std::strcat(out, "/");
    std::strcat(out, name);
}

static void remove_set(const char *base)
{
    static const char *const exts[] = {".shp", ".shx", ".dbf", ".qix"};
    char path[1024];
    std::size_t i;
    for (i = 0; i < sizeof exts / sizeof *exts; i++) {
        if (std::strlen(base) + 5 > sizeof path)
            std::abort();
        std::strcpy(path, base);
        std::strcat(path, exts[i]);
        std::remove(path);
    }
}

static void test_points(const char *base)
{
    static const double xs[3] = {1.5, -3.0, 10.0};
    static const double ys[3] = {2.5, 4.0, -7.0};
    SHPHandle h = SHPCreateLL(base, SHPT_POINT, &hooks);
    SHPObject *o;
    double lo[4], hi[4];
    int n, type, i;

    check(h != NULL, "SHPCreateLL point");
    if (!h)
        return;
    for (i = 0; i < 3; i++) {
        o = SHPCreateSimpleObject(SHPT_POINT, 1, &xs[i], &ys[i], NULL);
        check(SHPWriteObject(h, -1, o) == i, "SHPWriteObject index");
        SHPDestroyObject(o);
    }
    SHPClose(h);

    h = SHPOpenLL(base, "rb", &hooks);
    check(h != NULL, "SHPOpenLL point");
    if (!h)
        return;
    SHPGetInfo(h, &n, &type, lo, hi);
    check(n == 3 && type == SHPT_POINT, "point count and type");
    check(lo[0] == -3.0 && lo[1] == -7.0 && hi[0] == 10.0 && hi[1] == 4.0,
          "point bounds");
    for (i = 0; i < 3; i++) {
        o = SHPReadObject(h, i);
        check(o && o->nSHPType == SHPT_POINT && o->nVertices == 1 &&
                  o->padfX[0] == xs[i] && o->padfY[0] == ys[i],
              "point vertex");
        SHPDestroyObject(o);
    }
    check(SHPReadObject(h, 3) == NULL, "out-of-range shape is NULL");
    SHPClose(h);
}

static void test_polygon(const char *base)
{
    /* Counter-clockwise outer ring; the format wants clockwise. */
    static const double xs[5] = {0.0, 4.0, 4.0, 0.0, 0.0};
    static const double ys[5] = {0.0, 0.0, 4.0, 4.0, 0.0};
    SHPHandle h = SHPCreateLL(base, SHPT_POLYGON, &hooks);
    SHPObject *o;
    SHPTree *tree;
    double lo[4], hi[4], qlo[4] = {1, 1, 0, 0}, qhi[4] = {2, 2, 0, 0};
    double flo[4] = {10, 10, 0, 0}, fhi[4] = {11, 11, 0, 0};
    int part_start = 0, n, type, found, *ids;

    check(h != NULL, "SHPCreateLL polygon");
    if (!h)
        return;
    o = SHPCreateObject(SHPT_POLYGON, -1, 1, &part_start, NULL, 5, xs, ys, NULL,
                        NULL);
    check(o && SHPRewindObject(h, o) != 0, "ring is rewound to clockwise");
    check(SHPWriteObject(h, -1, o) == 0, "SHPWriteObject polygon");
    SHPDestroyObject(o);
    SHPClose(h);

    h = SHPOpenLL(base, "rb", &hooks);
    check(h != NULL, "SHPOpenLL polygon");
    if (!h)
        return;
    SHPGetInfo(h, &n, &type, lo, hi);
    check(n == 1 && type == SHPT_POLYGON, "polygon count and type");
    check(lo[0] == 0.0 && lo[1] == 0.0 && hi[0] == 4.0 && hi[1] == 4.0,
          "polygon bounds");
    o = SHPReadObject(h, 0);
    check(o && o->nParts == 1 && o->panPartStart[0] == 0 && o->nVertices == 5,
          "polygon structure");
    if (o && o->nVertices == 5) {
        static const double cx[5] = {0.0, 0.0, 4.0, 4.0, 0.0};
        static const double cy[5] = {0.0, 4.0, 4.0, 0.0, 0.0};
        check(std::memcmp(o->padfX, cx, sizeof cx) == 0 &&
                  std::memcmp(o->padfY, cy, sizeof cy) == 0,
              "clockwise ring vertices");
    }
    SHPDestroyObject(o);

    tree = SHPCreateTree(h, 2, 0, NULL, NULL);
    check(tree != NULL, "SHPCreateTree");
    if (tree) {
        ids = SHPTreeFindLikelyShapes(tree, qlo, qhi, &found);
        check(found == 1 && ids && ids[0] == 0, "tree query inside finds shape");
        std::free(ids);
        ids = SHPTreeFindLikelyShapes(tree, flo, fhi, &found);
        check(found == 0, "tree query outside finds nothing");
        std::free(ids);
        SHPDestroyTree(tree);
    }
    SHPClose(h);
}

static void test_dbf(const char *base)
{
    DBFHandle h = DBFCreateLL(base, "LDID/87", &hooks);
    char name[16];
    int width, decimals, i;

    check(h != NULL, "DBFCreateLL");
    if (!h)
        return;
    check(DBFAddField(h, "NAME", FTString, 12, 0) == 0, "add string field");
    check(DBFAddField(h, "COUNT", FTInteger, 6, 0) == 1, "add integer field");
    check(DBFAddField(h, "RATIO", FTDouble, 12, 3) == 2, "add double field");
    check(DBFWriteStringAttribute(h, 0, 0, "alpha") != 0, "write string 0");
    check(DBFWriteIntegerAttribute(h, 0, 1, 42) != 0, "write integer 0");
    check(DBFWriteDoubleAttribute(h, 0, 2, 3.125) != 0, "write double 0");
    check(DBFWriteStringAttribute(h, 1, 0, "beta") != 0, "write string 1");
    check(DBFWriteIntegerAttribute(h, 1, 1, -7) != 0, "write integer 1");
    DBFWriteNULLAttribute(h, 1, 2);
    DBFClose(h);

    h = DBFOpenLL(base, "rb", &hooks);
    check(h != NULL, "DBFOpenLL");
    if (!h)
        return;
    check(DBFGetRecordCount(h) == 2 && DBFGetFieldCount(h) == 3,
          "dbf record and field counts");
    check(DBFGetFieldInfo(h, 2, name, &width, &decimals) == FTDouble &&
              std::strcmp(name, "RATIO") == 0 && width == 12 && decimals == 3,
          "double field definition");
    check(DBFGetFieldIndex(h, "COUNT") == 1, "field index by name");
    check(std::strcmp(DBFReadStringAttribute(h, 0, 0), "alpha") == 0, "string 0");
    check(DBFReadIntegerAttribute(h, 0, 1) == 42, "integer 0");
    check(DBFReadDoubleAttribute(h, 0, 2) == 3.125, "double 0");
    check(std::strcmp(DBFReadStringAttribute(h, 1, 0), "beta") == 0, "string 1");
    check(DBFReadIntegerAttribute(h, 1, 1) == -7, "integer 1");
    check(DBFIsAttributeNULL(h, 1, 2) != 0, "NULL attribute");
    check(DBFIsAttributeNULL(h, 0, 2) == 0, "non-NULL attribute");
    for (i = 0; i < 2; i++)
        check(DBFIsRecordDeleted(h, i) == 0, "record not deleted");
    DBFClose(h);
}

int main(void)
{
    char pts[1024], poly[1024], dbf[1024];

    SASetupDefaultHooks(&defaults);
    hooks = defaults;
    hooks.FOpen = counting_open;
    hooks.Error = counting_error;

    make_path(pts, sizeof pts, "cabin_shapelib_pts");
    make_path(poly, sizeof poly, "cabin_shapelib_poly");
    make_path(dbf, sizeof dbf, "cabin_shapelib_attr");
    remove_set(pts);
    remove_set(poly);
    remove_set(dbf);

    test_points(pts);
    test_polygon(poly);
    test_dbf(dbf);

    check(opens >= 10, "custom FOpen hook was used for the files");
    check(errors == 0, "no error reported through the Error hook");

    remove_set(pts);
    remove_set(poly);
    remove_set(dbf);
    check(SHPOpenLL(pts, "rb", &hooks) == NULL, "removed set no longer opens");

    if (ok)
        std::puts("shapelib OK");
    return ok ? 0 : 1;
}
