#include <argon2.h>

#include <cstdio>
#include <cstring>

int main() {
  unsigned char out[32];
  char enc[128];
  // src/test.c vector: Argon2id, t=2, m=64 MiB, p=1.
  static const unsigned char want[32] = {
      0x09, 0x31, 0x61, 0x15, 0xd5, 0xcf, 0x24, 0xed, 0x5a, 0x15, 0xa3,
      0x1a, 0x3b, 0xa3, 0x26, 0xe5, 0xcf, 0x32, 0xed, 0xc2, 0x47, 0x02,
      0x98, 0x7c, 0x02, 0xb6, 0x56, 0x6f, 0x61, 0x91, 0x3c, 0xf7};
  if (argon2id_hash_raw(2, 1 << 16, 1, "password", 8, "somesalt", 8, out, 32) != ARGON2_OK) return 1;
  if (std::memcmp(out, want, 32) != 0) return 2;
  if (argon2id_hash_encoded(2, 1 << 16, 1, "password", 8, "somesalt", 8, 32,
                            enc, sizeof enc) != ARGON2_OK) return 3;
  if (std::strcmp(enc, "$argon2id$v=19$m=65536,t=2,p=1$c29tZXNhbHQ"
                       "$CTFhFdXPJO1aFaMaO6Mm5c8y7cJHAph8ArZWb2GRPPc") != 0) return 4;
  if (argon2id_verify(enc, "password", 8) != ARGON2_OK) return 5;
  if (argon2id_verify(enc, "passwore", 8) != ARGON2_VERIFY_MISMATCH) return 6;
  std::puts("argon2 ok");
  return 0;
}
