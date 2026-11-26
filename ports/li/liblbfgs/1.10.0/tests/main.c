#include <lbfgs.h>

#include <math.h>
#include <stdio.h>

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct trace {
    int calls;
    int last_k;
    int monotone;
    lbfgsfloatval_t last_fx;
};

/* Rosenbrock: f = (1-x0)^2 + 100 (x1-x0^2)^2, minimum 0 at (1, 1). */
static lbfgsfloatval_t evaluate(void *instance, const lbfgsfloatval_t *x, lbfgsfloatval_t *g,
                                const int n, const lbfgsfloatval_t step)
{
    lbfgsfloatval_t t1 = 1.0 - x[0], t2 = x[1] - x[0] * x[0];
    (void)instance; (void)n; (void)step;
    g[0] = -2.0 * t1 - 400.0 * x[0] * t2;
    g[1] = 200.0 * t2;
    return t1 * t1 + 100.0 * t2 * t2;
}

static int progress(void *instance, const lbfgsfloatval_t *x, const lbfgsfloatval_t *g,
                    const lbfgsfloatval_t fx, const lbfgsfloatval_t xnorm,
                    const lbfgsfloatval_t gnorm, const lbfgsfloatval_t step,
                    int n, int k, int ls)
{
    struct trace *t = instance;
    (void)x; (void)g; (void)xnorm; (void)gnorm; (void)step; (void)n; (void)ls;
    if (t->calls > 0 && (k != t->last_k + 1 || fx > t->last_fx)) t->monotone = 0;
    t->calls++;
    t->last_k = k;
    t->last_fx = fx;
    return 0;
}

static int cancel(void *instance, const lbfgsfloatval_t *x, const lbfgsfloatval_t *g,
                  const lbfgsfloatval_t fx, const lbfgsfloatval_t xnorm,
                  const lbfgsfloatval_t gnorm, const lbfgsfloatval_t step,
                  int n, int k, int ls)
{
    (void)instance; (void)x; (void)g; (void)fx; (void)xnorm; (void)gnorm; (void)step;
    (void)n; (void)k; (void)ls;
    return 77;
}

int main(void)
{
    lbfgs_parameter_t p;
    lbfgsfloatval_t fx = -1.0;
    lbfgsfloatval_t *x;
    struct trace t = {0, 0, 1, 0.0};
    int ret;

    lbfgs_parameter_init(&p);
    check(p.m == 6, "default m");
    check(p.epsilon == 1e-5, "default epsilon");
    check(p.past == 0, "default past");
    check(p.delta == 1e-5, "default delta");
    check(p.max_iterations == 0, "default max_iterations");
    check(p.linesearch == LBFGS_LINESEARCH_DEFAULT, "default linesearch");
    check(p.max_linesearch == 40, "default max_linesearch");
    check(p.min_step == 1e-20, "default min_step");
    check(p.max_step == 1e20, "default max_step");
    check(p.ftol == 1e-4, "default ftol");
    check(p.wolfe == 0.9, "default wolfe");
    check(p.gtol == 0.9, "default gtol");
    check(p.xtol == 1e-16, "default xtol");
    check(p.orthantwise_c == 0.0, "default orthantwise_c");
    check(p.orthantwise_start == 0, "default orthantwise_start");
    check(p.orthantwise_end == -1, "default orthantwise_end");

    x = lbfgs_malloc(2);
    check(x != NULL, "lbfgs_malloc");
    if (x == NULL) return 1;
    x[0] = -1.2;
    x[1] = 1.0;
    ret = lbfgs(2, x, &fx, evaluate, progress, &t, &p);
    check(ret == LBFGS_SUCCESS, "Rosenbrock converges");
    check(t.calls >= 10, "progress callback ran for many iterations");
    check(t.monotone, "iteration counter steps by one and fx never rises");
    check(fx < 1e-9, "final value near zero");
    check(fabs(x[0] - 1.0) < 1e-4 && fabs(x[1] - 1.0) < 1e-4, "minimiser is (1, 1)");

    x[0] = -1.2;
    x[1] = 1.0;
    check(lbfgs(2, x, &fx, evaluate, cancel, NULL, &p) == 77, "progress callback cancels");

    x[0] = 1.0;
    x[1] = 1.0;
    check(lbfgs(2, x, &fx, evaluate, NULL, NULL, &p) == LBFGS_ALREADY_MINIMIZED,
          "start at the minimum");

    p.epsilon = -1.0;
    check(lbfgs(2, x, &fx, evaluate, NULL, NULL, &p) == LBFGSERR_INVALID_EPSILON,
          "invalid epsilon");
    lbfgs_free(x);

    if (failures == 0) puts("ok");
    return failures != 0;
}
