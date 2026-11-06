/* libtommath's C API from C99: radix conversion, arithmetic, division,
   modular exponentiation and inverses, roots, primality testing (whose
   random bases come from mp_rand and so from the platform entropy
   source), fixed-width and binary conversions, against values computed
   independently. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <tommath.h>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

/* Whether a's representation in `radix` is `expected`. */
static int radix_equals(const mp_int *a, int radix, const char *expected) {
  char buf[128];
  size_t written = 0;
  return mp_to_radix(a, buf, sizeof buf, &written, radix) == MP_OKAY &&
         strcmp(buf, expected) == 0;
}

/* Whether mp_prime_is_prime with `trials` random bases succeeds and
   answers `expected`. */
static int prime_test_is(const mp_int *a, int trials, mp_bool expected) {
  mp_bool result = MP_NO;
  return mp_prime_is_prime(a, trials, &result) == MP_OKAY && result == expected;
}

int main(void) {
  mp_int a, b, c, d;
  unsigned char bin[8];
  size_t written = 0;
  int i;

  if (mp_init_multi(&a, &b, &c, &d, NULL) != MP_OKAY) {
    fprintf(stderr, "mp_init_multi failed\n");
    return 1;
  }

  /* Radix conversion and multiplication. */
  CHECK(mp_read_radix(&a, "123456789012345678901234567890", 10) == MP_OKAY);
  CHECK(mp_read_radix(&b, "987654321098765432109876543210", 10) == MP_OKAY);
  CHECK(mp_mul(&a, &b, &c) == MP_OKAY);
  CHECK(radix_equals(&c, 10,
                     "121932631137021795226185032733622923332237463801111263526900"));
  CHECK(mp_read_radix(&a, "-DEADBEEFCAFEBABE0123456789", 16) == MP_OKAY);
  CHECK(radix_equals(&a, 16, "-DEADBEEFCAFEBABE0123456789"));

  /* 30! and its division by 2^64. */
  mp_set(&a, 1u);
  for (i = 2; i <= 30; ++i) {
    CHECK(mp_mul_d(&a, (mp_digit)i, &a) == MP_OKAY);
  }
  CHECK(radix_equals(&a, 10, "265252859812191058636308480000000"));
  CHECK(mp_2expt(&b, 64) == MP_OKAY);
  CHECK(mp_div(&a, &b, &c, &d) == MP_OKAY);
  CHECK(radix_equals(&c, 10, "14379386343318"));
  CHECK(radix_equals(&d, 10, "9682165104862298112"));

  /* Number theory. */
  mp_set(&a, 4u);
  mp_set(&b, 13u);
  mp_set(&c, 497u);
  CHECK(mp_exptmod(&a, &b, &c, &d) == MP_OKAY && mp_cmp_d(&d, 445u) == MP_EQ);
  mp_set(&a, 17u);
  mp_set_u64(&b, 3120u);
  CHECK(mp_invmod(&a, &b, &c) == MP_OKAY && mp_get_i64(&c) == 2753);
  mp_set_u64(&a, 462u);
  mp_set_u64(&b, 1071u);
  CHECK(mp_gcd(&a, &b, &c) == MP_OKAY && mp_cmp_d(&c, 21u) == MP_EQ);
  CHECK(mp_lcm(&a, &b, &c) == MP_OKAY && mp_get_i64(&c) == 23562);
  CHECK(mp_read_radix(&a, "10000000000000000000000000000000000000000", 10) ==
        MP_OKAY);
  CHECK(mp_sqrt(&a, &b) == MP_OKAY && radix_equals(&b, 10, "100000000000000000000"));

  /* Primality: 2^127 - 1 is prime; 561 (a Carmichael number) and
     2^128 + 1 (the composite Fermat number F7) are not. */
  CHECK(mp_2expt(&a, 127) == MP_OKAY && mp_sub_d(&a, 1u, &a) == MP_OKAY);
  CHECK(prime_test_is(&a, mp_prime_rabin_miller_trials(127), MP_YES));
  mp_set_u64(&a, 561u);
  CHECK(prime_test_is(&a, 8, MP_NO));
  CHECK(mp_2expt(&a, 128) == MP_OKAY && mp_add_d(&a, 1u, &a) == MP_OKAY);
  CHECK(prime_test_is(&a, 8, MP_NO));

  /* mp_rand draws `digits` digits with a non-zero top digit. */
  CHECK(mp_rand(&a, 4) == MP_OKAY);
  CHECK(mp_count_bits(&a) > 3 * MP_DIGIT_BIT && mp_count_bits(&a) <= 4 * MP_DIGIT_BIT);

  /* Fixed-width and binary conversions. */
  mp_set_i64(&a, INT64_MIN);
  CHECK(mp_get_i64(&a) == INT64_MIN && mp_isneg(&a));
  CHECK(mp_2expt(&a, 60) == MP_OKAY && mp_get_double(&a) == 1152921504606846976.0);
  mp_set_u64(&a, UINT64_C(0x0102030405));
  CHECK(mp_ubin_size(&a) == 5u);
  CHECK(mp_to_ubin(&a, bin, sizeof bin, &written) == MP_OKAY && written == 5u &&
        memcmp(bin, "\x01\x02\x03\x04\x05", 5) == 0);
  CHECK(mp_from_ubin(&b, bin, written) == MP_OKAY && mp_cmp(&a, &b) == MP_EQ);

  CHECK(strcmp(mp_error_to_string(MP_VAL), "Value out of range") == 0);

  mp_clear_multi(&a, &b, &c, &d, NULL);
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
