/* qhull's reentrant C library.  Each run gets its own qhT and must free
   everything it allocated: the convex hull of a cube with interior
   points, the Delaunay triangulation of a 3x3 grid, and an input error,
   which qhull reports through its exit code and libqhull_r's
   qh_fprintf. */
#include <math.h>
#include <stdio.h>

#include <libqhull_r/qhull_ra.h>

static int failures = 0;

static void check(int ok, int line, const char *what) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond) != 0, __LINE__, #cond)

static int near(double a, double b) {
  return fabs(a - b) < 1e-12;
}

/* Whether p is one of the first count input points: qh_pointid returns
   negative ids for qhull's own points. */
static int input_point(qhT *qh, pointT *p, int count) {
  int id = qh_pointid(qh, p);
  return id >= 0 && id < count;
}

static void free_qhull(qhT *qh) {
  int curlong;
  int totlong;
  qh_freeqhull(qh, !qh_ALL);
  qh_memfreeshort(qh, &curlong, &totlong);
  CHECK(curlong == 0 && totlong == 0);
}

static void check_convex_hull(void) {
  coordT points[11 * 3];
  char flags[] = "qhull FA";
  qhT qh_qh;
  qhT *qh = &qh_qh;
  facetT *facet;
  vertexT *vertex;
  int i;
  int exitcode;

  /* The unit cube's corners, then three points inside it. */
  for (i = 0; i < 8; ++i) {
    points[3 * i] = (coordT)(i & 1);
    points[3 * i + 1] = (coordT)((i >> 1) & 1);
    points[3 * i + 2] = (coordT)((i >> 2) & 1);
  }
  for (i = 8; i < 11; ++i) {
    points[3 * i] = 0.25 * (i - 7);
    points[3 * i + 1] = 0.5;
    points[3 * i + 2] = 0.5;
  }
  qh_zero(qh, stderr);
  exitcode = qh_new_qhull(qh, 3, 11, points, False, flags, NULL, stderr);
  CHECK(exitcode == 0);
  if (exitcode == 0) {
    /* Qhull merges coplanar facets by default: one facet per face. */
    CHECK(qh->num_facets == 6);
    CHECK(qh->num_vertices == 8);
    CHECK(near(qh->totarea, 6.0));
    CHECK(near(qh->totvol, 1.0));
    FORALLfacets {
      int axes = 0;
      int k;
      CHECK(qh_setsize(qh, facet->vertices) == 4);
      for (k = 0; k < 3; ++k) {
        if (near(fabs(facet->normal[k]), 1.0)) {
          ++axes;
        }
      }
      CHECK(axes == 1);
    }
    FORALLvertices {
      CHECK(input_point(qh, vertex->point, 8));
    }
  }
  free_qhull(qh);
}

static void check_delaunay(void) {
  coordT points[9 * 2];
  char flags[] = "qhull d Qt Qbb Qc Qz";
  qhT qh_qh;
  qhT *qh = &qh_qh;
  facetT *facet;
  int i;
  int exitcode;
  int triangles = 0;
  double area = 0.0;

  for (i = 0; i < 9; ++i) {
    points[2 * i] = (coordT)(i % 3);
    points[2 * i + 1] = (coordT)(i / 3);
  }
  qh_zero(qh, stderr);
  exitcode = qh_new_qhull(qh, 2, 9, points, False, flags, NULL, stderr);
  CHECK(exitcode == 0);
  if (exitcode == 0) {
    /* The lower facets of the lifted points are the triangles: 'Qt'
       splits each cocircular square cell in two.  Their vertices are
       input points, never the point at infinity that 'Qz' adds (id 9). */
    FORALLfacets {
      if (!facet->upperdelaunay) {
        pointT *a = SETelemt_(facet->vertices, 0, vertexT)->point;
        pointT *b = SETelemt_(facet->vertices, 1, vertexT)->point;
        pointT *c = SETelemt_(facet->vertices, 2, vertexT)->point;
        CHECK(qh_setsize(qh, facet->vertices) == 3);
        CHECK(input_point(qh, a, 9) && input_point(qh, b, 9) &&
              input_point(qh, c, 9));
        area += 0.5 * fabs((b[0] - a[0]) * (c[1] - a[1]) -
                           (c[0] - a[0]) * (b[1] - a[1]));
        ++triangles;
      }
    }
    CHECK(triangles == 8);
    CHECK(near(area, 4.0));
  }
  free_qhull(qh);
}

static void check_input_error(void) {
  /* Three points cannot span a 3-d simplex. */
  coordT points[3 * 3] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  char flags[] = "qhull";
  qhT qh_qh;
  qhT *qh = &qh_qh;
  int exitcode;

  qh_zero(qh, stderr);
  fprintf(stderr, "expected qhull input error QH6214:\n");
  exitcode = qh_new_qhull(qh, 3, 3, points, False, flags, NULL, stderr);
  CHECK(exitcode == qh_ERRinput);
  CHECK(qh->last_errcode == 6214);
  free_qhull(qh);
}

int main(void) {
  QHULL_LIB_CHECK
  check_convex_hull();
  check_delaunay();
  check_input_error();
  if (failures != 0) {
    return 1;
  }
  printf("qhull %s: C API OK\n", qh_version);
  return 0;
}
