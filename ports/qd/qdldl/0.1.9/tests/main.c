/* The C consumer: the generated version and type headers, the elimination
   tree and column counts of a 3x3 quasidefinite matrix, its LDL' factors and
   the full and the two triangular solves, a zero pivot, and a lower-triangular
   input that the elimination tree rejects. */
#include <qdldl.h>

#include <math.h>
#include <stdio.h>

static int ok = 1;

static void check(int pass, const char *what) {
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

static int near(double a, double b) { return fabs(a - b) < 1e-12; }

int main(void) {
    /* Upper triangle of [[4,1,0],[1,3,1],[0,1,-2]] in CSC form. */
    QDLDL_int Ap[] = {0, 1, 3, 5};
    QDLDL_int Ai[] = {0, 0, 1, 1, 2};
    QDLDL_float Ax[] = {4, 1, 3, 1, -2};
    QDLDL_int n = 3;

    QDLDL_int work[3], Lnz[3], etree[3];
    QDLDL_int Lp[4], Li[2], iwork[9];
    QDLDL_float Lx[2], D[3], Dinv[3], fwork[3];
    QDLDL_bool bwork[3];
    QDLDL_float x[3] = {6, 10, -4}; /* A * {1, 2, 3} */
    QDLDL_float y[3] = {6, 10, -4};

    check(QDLDL_VERSION_MAJOR == 0 && QDLDL_VERSION_MINOR == 1 && QDLDL_VERSION_PATCH == 9, "version");
    check(sizeof(QDLDL_float) == sizeof(double) && sizeof(QDLDL_bool) == 1, "types");

    check(QDLDL_etree(n, Ap, Ai, work, Lnz, etree) == 2, "etree total");
    check(Lnz[0] == 1 && Lnz[1] == 1 && Lnz[2] == 0, "Lnz");
    check(etree[0] == 1 && etree[1] == 2 && etree[2] == -1, "etree");

    check(QDLDL_factor(n, Ap, Ai, Ax, Lp, Li, Lx, D, Dinv, Lnz, etree, bwork, iwork, fwork) == 2,
          "positive pivots");
    check(Lp[0] == 0 && Lp[1] == 1 && Lp[2] == 2 && Lp[3] == 2, "Lp");
    check(Li[0] == 1 && Li[1] == 2, "Li");
    check(near(Lx[0], 0.25) && near(Lx[1], 4.0 / 11.0), "Lx");
    check(near(D[0], 4) && near(D[1], 2.75) && near(D[2], -26.0 / 11.0), "D");
    check(near(Dinv[0], 0.25) && near(Dinv[1], 4.0 / 11.0) && near(Dinv[2], -11.0 / 26.0), "Dinv");

    QDLDL_solve(n, Lp, Li, Lx, Dinv, x);
    check(near(x[0], 1) && near(x[1], 2) && near(x[2], 3), "solve");

    /* Raw triangular solves: (L+I) z = {6, 10, -4}, then (L+I)' w = z. */
    QDLDL_Lsolve(n, Lp, Li, Lx, y);
    check(near(y[0], 6) && near(y[1], 8.5) && near(y[2], -4 - 4.0 / 11.0 * 8.5), "Lsolve");
    QDLDL_Ltsolve(n, Lp, Li, Lx, y);
    check(near(y[2], -4 - 4.0 / 11.0 * 8.5) && near(y[1], 8.5 - 4.0 / 11.0 * y[2]) &&
              near(y[0], 6 - 0.25 * y[1]),
          "Ltsolve");

    {
        /* [[0,1],[1,1]] has a zero first pivot. */
        QDLDL_int zAp[] = {0, 1, 3}, zAi[] = {0, 0, 1};
        QDLDL_float zAx[] = {0, 1, 1};
        QDLDL_int zLp[3], zLi[1], zLnz[2], zEtree[2], zWork[6];
        QDLDL_float zLx[1], zD[2], zDinv[2], zF[2];
        QDLDL_bool zB[2];
        check(QDLDL_etree(2, zAp, zAi, zWork, zLnz, zEtree) == 1, "zero-pivot etree");
        check(QDLDL_factor(2, zAp, zAi, zAx, zLp, zLi, zLx, zD, zDinv, zLnz, zEtree, zB, zWork, zF) == -1,
              "zero pivot");
    }

    {
        /* Column 0 holds a row below the diagonal: not upper triangular. */
        QDLDL_int lAp[] = {0, 2, 3}, lAi[] = {0, 1, 1};
        QDLDL_int lLnz[2], lEtree[2], lWork[2];
        check(QDLDL_etree(2, lAp, lAi, lWork, lLnz, lEtree) == -1, "lower-triangular input");
    }

    return ok ? 0 : 1;
}
