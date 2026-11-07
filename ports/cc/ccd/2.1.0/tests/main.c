/* Collides spheres and boxes through libccd's GJK, EPA and MPR entry
   points, and checks its vector and quaternion helpers. */
#include <ccd/ccd.h>
#include <ccd/quat.h>

#include <math.h>
#include <stdio.h>

#if !defined(CCD_SINGLE) || defined(CCD_DOUBLE)
#error "ccd/config.h must select single precision"
#endif

static int ok = 1;

static void check(int pass, const char *what)
{
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

static int near(ccd_real_t a, ccd_real_t b, ccd_real_t tolerance)
{
    return fabs((double)(a - b)) <= (double)tolerance;
}

static int near_vec(const ccd_vec3_t *v, ccd_real_t x, ccd_real_t y, ccd_real_t z,
                    ccd_real_t tolerance)
{
    return near(ccdVec3X(v), x, tolerance) && near(ccdVec3Y(v), y, tolerance) &&
           near(ccdVec3Z(v), z, tolerance);
}

struct sphere {
    ccd_vec3_t center;
    ccd_real_t radius;
};

struct box {
    ccd_vec3_t center;
    ccd_vec3_t half;
};

static void sphere_support(const void *obj, const ccd_vec3_t *dir, ccd_vec3_t *out)
{
    const struct sphere *s = (const struct sphere *)obj;
    ccd_vec3_t d;
    ccdVec3Copy(&d, dir);
    ccdVec3Normalize(&d);
    ccdVec3Scale(&d, s->radius);
    ccdVec3Copy(out, &s->center);
    ccdVec3Add(out, &d);
}

static void sphere_center(const void *obj, ccd_vec3_t *out)
{
    ccdVec3Copy(out, &((const struct sphere *)obj)->center);
}

static void box_support(const void *obj, const ccd_vec3_t *dir, ccd_vec3_t *out)
{
    const struct box *b = (const struct box *)obj;
    ccdVec3Set(out, ccdVec3X(&b->center) + ccdSign(ccdVec3X(dir)) * ccdVec3X(&b->half),
               ccdVec3Y(&b->center) + ccdSign(ccdVec3Y(dir)) * ccdVec3Y(&b->half),
               ccdVec3Z(&b->center) + ccdSign(ccdVec3Z(dir)) * ccdVec3Z(&b->half));
}

static void box_center(const void *obj, ccd_vec3_t *out)
{
    ccdVec3Copy(out, &((const struct box *)obj)->center);
}

static void spheres(void)
{
    struct sphere a, b, far_away;
    ccd_t ccd;
    ccd_real_t depth = 0;
    ccd_vec3_t dir, pos;

    /* Zeroed, so a failed call leaves nothing uninitialized to check. */
    ccdVec3Set(&dir, 0, 0, 0);
    ccdVec3Set(&pos, 0, 0, 0);
    ccdVec3Set(&a.center, 0, 0, 0);
    a.radius = 1;
    ccdVec3Set(&b.center, 0, CCD_REAL(1.5), 0);
    b.radius = 1;
    ccdVec3Set(&far_away.center, 3, 0, 0);
    far_away.radius = 1;

    CCD_INIT(&ccd);
    ccd.support1 = sphere_support;
    ccd.support2 = sphere_support;
    ccd.center1 = sphere_center;
    ccd.center2 = sphere_center;

    check(ccdGJKIntersect(&a, &b, &ccd) && ccdMPRIntersect(&a, &b, &ccd),
          "overlapping spheres intersect");
    check(!ccdGJKIntersect(&a, &far_away, &ccd) && !ccdMPRIntersect(&a, &far_away, &ccd),
          "distant spheres do not");

    /* The second sphere must move 0.5 along +y to only touch the first. */
    check(ccdMPRPenetration(&a, &b, &ccd, &depth, &dir, &pos) == 0, "ccdMPRPenetration");
    check(near(depth, CCD_REAL(0.5), CCD_REAL(0.001)) &&
              near_vec(&dir, 0, 1, 0, CCD_REAL(0.001)),
          "MPR's depth and direction");
    check(ccdVec3Y(&pos) > CCD_REAL(0.4) && ccdVec3Y(&pos) < CCD_REAL(1.1),
          "MPR's contact position lies in the overlap");
    check(ccdMPRPenetration(&a, &far_away, &ccd, &depth, &dir, &pos) == -1,
          "no MPR penetration without contact");
}

static void boxes(void)
{
    struct box a, b, apart;
    ccd_t ccd;
    ccd_real_t depth = 0;
    ccd_vec3_t dir, pos, sep;

    ccdVec3Set(&dir, 0, 0, 0);
    ccdVec3Set(&pos, 0, 0, 0);
    ccdVec3Set(&sep, 0, 0, 0);
    ccdVec3Set(&a.center, 0, 0, 0);
    ccdVec3Set(&a.half, 1, 1, 1);
    /* Overlaps a by 0.25 along x, 1.8 along y and 1.9 along z. */
    ccdVec3Set(&b.center, CCD_REAL(1.75), CCD_REAL(0.2), CCD_REAL(-0.1));
    ccdVec3Set(&b.half, 1, 1, 1);
    ccdVec3Set(&apart.center, 0, 0, CCD_REAL(2.5));
    ccdVec3Set(&apart.half, 1, 1, CCD_REAL(0.25));

    CCD_INIT(&ccd);
    ccd.support1 = box_support;
    ccd.support2 = box_support;
    ccd.center1 = box_center;
    ccd.center2 = box_center;

    check(ccdGJKPenetration(&a, &b, &ccd, &depth, &dir, &pos) == 0, "ccdGJKPenetration");
    check(near(depth, CCD_REAL(0.25), CCD_REAL(0.001)) &&
              near_vec(&dir, 1, 0, 0, CCD_REAL(0.001)),
          "EPA finds the shallowest axis");
    check(ccdGJKSeparate(&a, &b, &ccd, &sep) == 0 &&
              near_vec(&sep, CCD_REAL(0.25), 0, 0, CCD_REAL(0.001)),
          "ccdGJKSeparate's translation");
    check(ccdMPRPenetration(&a, &b, &ccd, &depth, &dir, &pos) == 0 &&
              near(depth, CCD_REAL(0.25), CCD_REAL(0.001)),
          "MPR agrees with EPA");

    check(!ccdGJKIntersect(&a, &apart, &ccd), "a gap of 1.25 along z");
    check(ccdGJKPenetration(&a, &apart, &ccd, &depth, &dir, &pos) == -1 &&
              ccdGJKSeparate(&a, &apart, &ccd, &sep) == -1,
          "no penetration or separation without contact");
}

static void geometry(void)
{
    ccd_vec3_t zero, p, a, b, c, witness, v;
    ccd_quat_t q;

    /* Exported data, which a consumer reaches only with CCD_STATIC_DEFINE
       on MSVC (ccd_export.h). */
    ccdVec3Set(&zero, 0, 0, 0);
    check(ccdVec3Eq(ccd_vec3_origin, &zero), "the exported origin");

    ccdVec3Set(&p, CCD_REAL(0.5), 2, 0);
    ccdVec3Set(&a, -1, 0, 0);
    ccdVec3Set(&b, 1, 0, 0);
    check(near(ccdVec3PointSegmentDist2(&p, &a, &b, &witness), 4, CCD_REAL(1e-5)) &&
              near_vec(&witness, CCD_REAL(0.5), 0, 0, CCD_REAL(1e-5)),
          "ccdVec3PointSegmentDist2");

    ccdVec3Set(&p, CCD_REAL(0.25), CCD_REAL(0.25), 3);
    ccdVec3Set(&c, 0, 1, 0);
    ccdVec3Set(&b, 1, 0, 0);
    ccdVec3Set(&a, 0, 0, 0);
    check(near(ccdVec3PointTriDist2(&p, &a, &b, &c, &witness), 9, CCD_REAL(1e-4)) &&
              near_vec(&witness, CCD_REAL(0.25), CCD_REAL(0.25), 0, CCD_REAL(1e-5)),
          "ccdVec3PointTriDist2 above the triangle");

    /* A quarter turn about z takes x to y. */
    ccdVec3Set(&v, 0, 0, 1);
    ccdQuatSetAngleAxis(&q, CCD_REAL(1.5707963), &v);
    ccdVec3Set(&v, 1, 0, 0);
    ccdQuatRotVec(&v, &q);
    check(near_vec(&v, 0, 1, 0, CCD_REAL(1e-5)), "ccdQuatRotVec");
}

int main(void)
{
    check(sizeof(ccd_real_t) == sizeof(float), "single-precision ccd_real_t");
    spheres();
    boxes();
    geometry();
    printf("ccd (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
