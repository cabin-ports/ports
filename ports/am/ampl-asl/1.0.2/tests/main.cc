// Reads the C test's problem with pfgh_read, the partially separable reader
// second-order solvers use, and checks values, first derivatives and the
// Hessian of the Lagrangian that fullhes computes:
//   minimize   x0^2 + x0*x1 + 3*x1
//   subject to x0^2*x1 + 2*x0 - x1 <= 10,  -5 <= x0 <= 5,  x1 >= 0.
#include <cstdio>
#include <cstdlib>
#include <string>

// After the standard headers: asl.h defines getenv, fprintf, snprintf and
// others as macros for ASL's own global versions, so std:: cannot name them.
#include "asl.h"

namespace {

const char *const nl_lines[] = {
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

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fputs("FAIL: ", stderr);
        std::fputs(what, stderr);
        std::fputc('\n', stderr);
        ++failures;
    }
}

bool write_nl(const std::string &path)
{
    std::FILE *out = std::fopen(path.c_str(), "wb");
    if (out == NULL)
        return false;
    for (std::size_t i = 0; i < sizeof nl_lines / sizeof nl_lines[0]; ++i) {
        std::fputs(nl_lines[i], out);
        std::fputc('\n', out);
    }
    return std::fclose(out) == 0;
}

}  // namespace

int main()
{
    const char *dir = getenv("CABIN_PROFILE_DIR");
    if (dir == NULL) {
        check(false, "CABIN_PROFILE_DIR");
        return 1;
    }
    const std::string stub = std::string(dir) + "/port-probe-ampl-asl-cc";
    const std::string path = stub + ".nl";
    if (!write_nl(path)) {
        check(false, "write the .nl file");
        return 1;
    }

    ASL *asl = ASL_alloc(ASL_read_pfgh);
    std::FILE *nl = jac0dim(stub.c_str(), static_cast<fint>(stub.size()));
    check(n_var == 2 && n_con == 1 && n_obj == 1 && nzc == 2, "jac0dim: sizes");
    pfgh_read(nl, 0);

    real x[2] = {3., 2.};
    fint ne = 0;
    check(objval(0, x, &ne) == 21. && ne == 0, "objval");
    real grad[2];
    objgrd(0, x, grad, &ne);
    check(ne == 0 && grad[0] == 8. && grad[1] == 6., "objgrd");
    real con[1];
    conval(x, con, &ne);
    check(ne == 0 && con[0] == 22., "conval");
    real jac[2];
    jacval(x, jac, &ne);
    check(ne == 0, "jacval");
    real dc[2] = {0., 0.};
    int entries = 0;
    for (cgrad *cg = Cgrad[0]; cg != NULL; cg = cg->next) {
        dc[cg->varno] = jac[cg->goff];
        ++entries;
    }
    check(entries == 2 && dc[0] == 14. && dc[1] == 8., "jacval: Cgrad");

    // Objective weight 1 (ow == NULL) and constraint multiplier 2: the
    // Hessian of f + 2*c at x, full and column-major.
    real y[1] = {2.};
    real h[4];
    fullhes(h, 2, 0, NULL, y);
    check(h[0] == 10. && h[1] == 13. && h[2] == 13. && h[3] == 0., "fullhes");

    ASL_free(&asl);
    check(asl == NULL, "ASL_free");
    std::remove(path.c_str());
    return failures != 0;
}
