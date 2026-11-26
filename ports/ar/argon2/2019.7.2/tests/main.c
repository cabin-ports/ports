#include <argon2.h>

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                  \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

static void hex(const unsigned char *in, size_t n, char *out) {
  size_t i;
  for (i = 0; i < n; ++i) sprintf(out + 2 * i, "%02x", in[i]);
}

/* RFC 9106 section 5 vectors: 4 lanes, 32 KiB, 3 passes, secret and data. */
static void rfc9106(argon2_type type, const char *want) {
  unsigned char out[32], pwd[32], salt[16], secret[8], ad[12];
  char got[65];
  argon2_context ctx;
  memset(pwd, 1, sizeof pwd);
  memset(salt, 2, sizeof salt);
  memset(secret, 3, sizeof secret);
  memset(ad, 4, sizeof ad);
  memset(&ctx, 0, sizeof ctx);
  ctx.out = out; ctx.outlen = sizeof out;
  ctx.pwd = pwd; ctx.pwdlen = sizeof pwd;
  ctx.salt = salt; ctx.saltlen = sizeof salt;
  ctx.secret = secret; ctx.secretlen = sizeof secret;
  ctx.ad = ad; ctx.adlen = sizeof ad;
  ctx.t_cost = 3; ctx.m_cost = 32; ctx.lanes = 4; ctx.threads = 4;
  ctx.version = ARGON2_VERSION_13;
  CHECK(argon2_ctx(&ctx, type) == ARGON2_OK);
  hex(out, sizeof out, got);
  CHECK(strcmp(got, want) == 0);
}

int main(void) {
  unsigned char out[32];
  char enc[128], got[65];
  const char *pwd = "password", *salt = "somesalt";
  const char *id_enc =
      "$argon2id$v=19$m=65536,t=2,p=1$c29tZXNhbHQ"
      "$CTFhFdXPJO1aFaMaO6Mm5c8y7cJHAph8ArZWb2GRPPc";

  /* src/test.c: Argon2id, and Argon2i at version 0x10 with two lanes. */
  CHECK(argon2_hash(2, 1 << 16, 1, pwd, 8, salt, 8, out, 32, enc, sizeof enc,
                    Argon2_id, ARGON2_VERSION_13) == ARGON2_OK);
  hex(out, 32, got);
  CHECK(strcmp(got, "09316115d5cf24ed5a15a31a3ba326e5cf32edc24702987c02b6566f61913cf7") == 0);
  CHECK(strcmp(enc, id_enc) == 0);
  CHECK(argon2id_verify(enc, pwd, 8) == ARGON2_OK);
  CHECK(argon2id_verify(enc, "passwore", 8) == ARGON2_VERIFY_MISMATCH);

  CHECK(argon2_hash(2, 1 << 8, 2, pwd, 8, salt, 8, out, 32, enc, sizeof enc,
                    Argon2_i, ARGON2_VERSION_10) == ARGON2_OK);
  hex(out, 32, got);
  CHECK(strcmp(got, "b6c11560a6a9d61eac706b79a2f97d68b4463aa3ad87e00c07e2b01e90c564fb") == 0);
  CHECK(strcmp(enc, "$argon2i$v=16$m=256,t=2,p=2$c29tZXNhbHQ"
                    "$tsEVYKap1h6scGt5ovl9aLRGOqOth+AMB+KwHpDFZPs") == 0);

  rfc9106(Argon2_d, "512b391b6f1162975371d30919734294f868e3be3984f3c1a13a4db9fabe4acb");
  rfc9106(Argon2_i, "c814d9d1dc7f37aa13f0d77f2494bda1c8de6b016dd388d29952a4c4672b6ce8");
  rfc9106(Argon2_id, "0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659");

  CHECK(argon2_hash(2, 1, 1, pwd, 8, salt, 8, out, 32, NULL, 0, Argon2_id,
                    ARGON2_VERSION_13) == ARGON2_MEMORY_TOO_LITTLE);
  CHECK(strcmp(argon2_error_message(ARGON2_DECODING_FAIL), "Decoding failed") == 0);
  CHECK(strcmp(argon2_type2string(Argon2_id, 0), "argon2id") == 0);

  if (failures) return 1;
  puts("argon2 ok");
  return 0;
}
