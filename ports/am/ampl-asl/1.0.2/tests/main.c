/* Writes a problem in the text .nl format, reads it with fg_read, and checks
   the bounds, starting point, values and first derivatives ASL computes:
     minimize   x0^2 + x0*x1 + 3*x1
     subject to x0^2*x1 + 2*x0 - x1 <= 10,  -5 <= x0 <= 5,  x1 >= 0,
   starting from (1.5, -0.5). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asl.h"

static const char *const nl_lines[] = {
    "g3 1 1 0",
    " 2 1 1 0 0",
    " 1 1",
    " 0 0",
    " 2 2 2",
    " 0 0 0 1",
    " 0 0 0 0 0",
    " 2 2",
    " 0 0",
    " 0 0 0 0 0",
    "C0", "o2", "o5", "v0", "n2", "v1",
    "O0 0", "o0", "o5", "v0", "n2", "o2", "v0", "v1",
    "x2", "0 1.5", "1 -0.5",
    "r", "1 10",
    "b", "0 -5 5", "2 0",
    "k1", "1",
    "J0 2", "0 2", "1 -1",
    "G0 2", "0 0", "1 3",
};

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static int write_nl(const char *path)
{
    FILE *out = fopen(path, "wb");
    size_t i;

    if (out == NULL)
        return 0;
    for (i = 0; i < sizeof nl_lines / sizeof nl_lines[0]; i++) {
        fputs(nl_lines[i], out);
        fputc('\n', out);
    }
    return fclose(out) == 0;
}

int main(void)
{
    const char *dir = getenv("CABIN_PROFILE_DIR");
    char stub[4096], path[4096];
    ASL *asl;
    FILE *nl;
    cgrad *cg;
    real x[2], grad[2], jac[2], dc[2], con[1], f;
    fint ne = 0;
    int entries = 0;

    if (dir == NULL || strlen(dir) + 32 > sizeof stub) {
        fprintf(stderr, "FAIL: CABIN_PROFILE_DIR\n");
        return 1;
    }
    strcpy(stub, dir);
    strcat(stub, "/port-probe-ampl-asl-c");
    strcpy(path, stub);
    strcat(path, ".nl");
    if (!write_nl(path)) {
        fprintf(stderr, "FAIL: write %s\n", path);
        return 1;
    }

    asl = ASL_alloc(ASL_read_fg);
    nl = jac0dim(stub, (fint)strlen(stub));
    check(n_var == 2 && n_con == 1 && n_obj == 1 && nzc == 2, "jac0dim: sizes");
    want_xpi0 = 1;
    fg_read(nl, 0);

    check(X0 != NULL && X0[0] == 1.5 && X0[1] == -0.5, "fg_read: X0");
    check(LUv[0] == -5. && LUv[1] == 5. && LUv[2] == 0. && LUv[3] == Infinity,
          "fg_read: LUv");
    check(LUrhs[0] == negInfinity && LUrhs[1] == 10., "fg_read: LUrhs");

    x[0] = 3.;
    x[1] = 2.;
    f = objval(0, x, &ne);
    check(ne == 0 && f == 21., "objval");
    objgrd(0, x, grad, &ne);
    check(ne == 0 && grad[0] == 8. && grad[1] == 6., "objgrd");
    conval(x, con, &ne);
    check(ne == 0 && con[0] == 22., "conval");
    jacval(x, jac, &ne);
    check(ne == 0, "jacval");
    dc[0] = dc[1] = 0.;
    for (cg = Cgrad[0]; cg != NULL; cg = cg->next) {
        dc[cg->varno] = jac[cg->goff];
        entries++;
    }
    check(entries == 2 && dc[0] == 14. && dc[1] == 8., "jacval: Cgrad");

    /* A new point is evaluated afresh. */
    x[0] = -1.;
    x[1] = 4.;
    check(objval(0, x, &ne) == 9. && ne == 0, "objval at a new point");
    conval(x, con, &ne);
    check(ne == 0 && con[0] == -2., "conval at a new point");

    ASL_free(&asl);
    check(asl == NULL, "ASL_free");
    remove(path);
    return failures != 0;
}
