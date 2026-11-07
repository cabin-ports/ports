// libtommath's C API from C++11 through a small RAII wrapper: a toy RSA
// round trip (key from mp_invmod, mp_exptmod both ways), Fibonacci(100)
// by repeated mp_add, and mp_prime_next_prime above 10^20.
#include <cstdio>
#include <string>

#include <tommath.h>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc: check failed: %s\n", what);
    ++failures;
  }
}

struct Int {
  mp_int v;
  Int() {
    if (mp_init(&v) != MP_OKAY) throw "mp_init";
  }
  explicit Int(const char* decimal) : Int() {
    if (mp_read_radix(&v, decimal, 10) != MP_OKAY) throw "mp_read_radix";
  }
  ~Int() { mp_clear(&v); }
  Int(const Int&) = delete;
  Int& operator=(const Int&) = delete;

  std::string str() const {
    char buf[64];
    size_t written = 0;
    if (mp_to_radix(&v, buf, sizeof buf, &written, 10) != MP_OKAY) return "<error>";
    return buf;
  }
};

}  // namespace

int main() {
  // RSA with p = 61, q = 53: n = 3233, phi = 3120, e = 17, d = 2753.
  Int n("3233"), phi("3120"), e("17"), d, m("65"), c, back;
  check(mp_invmod(&e.v, &phi.v, &d.v) == MP_OKAY && d.str() == "2753", "d = e^-1 mod phi");
  check(mp_exptmod(&m.v, &e.v, &n.v, &c.v) == MP_OKAY && c.str() == "2790", "encrypt");
  check(mp_exptmod(&c.v, &d.v, &n.v, &back.v) == MP_OKAY && back.str() == "65", "decrypt");

  Int x("0"), y("1"), t;
  for (int i = 0; i < 100; ++i) {
    check(mp_add(&x.v, &y.v, &t.v) == MP_OKAY, "mp_add");
    mp_exch(&x.v, &y.v);
    mp_exch(&y.v, &t.v);
  }
  check(x.str() == "354224848179261915075", "Fibonacci(100)");

  Int p("100000000000000000000");
  check(mp_prime_next_prime(&p.v, 8, 0) == MP_OKAY && p.str() == "100000000000000000039",
        "next prime after 10^20");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
