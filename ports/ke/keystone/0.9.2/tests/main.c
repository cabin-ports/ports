/* Assembles on every architecture the default build carries and checks
 * the version, mode and error paths.  Expected bytes come from upstream's
 * suite/regress/test_all_archs.py (line noted per case) and x64_loop.py;
 * inc and dec are FF /0 and FF /1 with REX.W, and EVM's ADD is opcode
 * 0x01. */
#include <keystone/keystone.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

struct vector {
  const char *name;
  ks_arch arch;
  int mode;
  const char *text;
  const char *bytes;
  size_t size;
  size_t statements;
};

static const struct vector vectors[] = {
    /* :35 */
    {"ARM", KS_ARCH_ARM, KS_MODE_ARM, "sub r1, r2, r5", "\x05\x10\x42\xe0", 4,
     1},
    /* :36 */
    {"ARM big-endian", KS_ARCH_ARM, KS_MODE_ARM + KS_MODE_BIG_ENDIAN,
     "sub r1, r2, r5", "\xe0\x42\x10\x05", 4, 1},
    /* :37 */
    {"Thumb", KS_ARCH_ARM, KS_MODE_THUMB, "movs r4, #0xf0", "\xf0\x24", 2, 1},
    /* :41 */
    {"ARM64", KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN, "ldr w1, [sp, #0x8]",
     "\xe1\x0b\x40\xb9", 4, 1},
    /* :44 */
    {"Hexagon", KS_ARCH_HEXAGON, KS_MODE_BIG_ENDIAN,
     "v23.w=vavg(v11.w,v2.w):rnd", "\xd7\xcb\xe2\x1c", 4, 1},
    /* :47 */
    {"MIPS32", KS_ARCH_MIPS, KS_MODE_MIPS32, "and $9, $6, $7",
     "\x24\x48\xc7\x00", 4, 1},
    /* :50 */
    {"MIPS64 big-endian", KS_ARCH_MIPS, KS_MODE_MIPS64 + KS_MODE_BIG_ENDIAN,
     "and $9, $6, $7", "\x00\xc7\x48\x24", 4, 1},
    /* :53 */
    {"PPC32 big-endian", KS_ARCH_PPC, KS_MODE_PPC32 + KS_MODE_BIG_ENDIAN,
     "add 1, 2, 3", "\x7c\x22\x1a\x14", 4, 1},
    /* :54 */
    {"PPC64", KS_ARCH_PPC, KS_MODE_PPC64, "add 1, 2, 3", "\x14\x1a\x22\x7c", 4,
     1},
    /* :58 */
    {"SPARC32", KS_ARCH_SPARC, KS_MODE_SPARC32 + KS_MODE_LITTLE_ENDIAN,
     "add %g1, %g2, %g3", "\x02\x40\x00\x86", 4, 1},
    /* :62 */
    {"SystemZ", KS_ARCH_SYSTEMZ, KS_MODE_BIG_ENDIAN, "a %r0, 4095(%r15,%r1)",
     "\x5a\x0f\x1f\xff", 4, 1},
    /* :28 */
    {"x86-16", KS_ARCH_X86, KS_MODE_16, "add eax, ecx", "\x66\x01\xc8", 3, 1},
    /* :30, then FF /0 and FF /1 */
    {"x86-64", KS_ARCH_X86, KS_MODE_64, "add rax, rcx; inc rcx; dec rdx",
     "\x48\x01\xc8\x48\xff\xc1\x48\xff\xca", 9, 3},
    {"EVM", KS_ARCH_EVM, 0, "add", "\x01", 1, 1},
};

/* Assembles text at address and checks the encoding and statement count. */
static void expect(const char *name, ks_engine *ks, const char *text,
                   uint64_t address, const char *bytes, size_t size,
                   size_t statements) {
  unsigned char *encoding = NULL;
  size_t encoding_size = 0, count = 0;
  int rc = ks_asm(ks, text, address, &encoding, &encoding_size, &count);
  if (rc != 0) {
    fprintf(stderr, "FAIL: %s: ks_asm returned %d (%s)\n", name, rc,
            ks_strerror(ks_errno(ks)));
    ++failures;
    return;
  }
  if (encoding_size != size || memcmp(encoding, bytes, size) != 0) {
    size_t i;
    fprintf(stderr, "FAIL: %s: encoded", name);
    for (i = 0; i < encoding_size; ++i)
      fprintf(stderr, " %02x", encoding[i]);
    fprintf(stderr, "\n");
    ++failures;
  }
  if (count != statements) {
    fprintf(stderr, "FAIL: %s: %lu statements\n", name, (unsigned long)count);
    ++failures;
  }
  ks_free(encoding);
}

int main(void) {
  unsigned int major = 99, minor = 99;
  size_t i;
  ks_engine *ks = NULL;
  unsigned char *encoding = NULL;
  size_t encoding_size = 0, count = 0;
  int arch;

  check(ks_version(&major, &minor) == 9, "ks_version");
  check(KS_MAKE_VERSION(0, 9) == 9, "KS_MAKE_VERSION");
  check(major == 0 && minor == 9, "ks_version major and minor");

  for (arch = KS_ARCH_ARM; arch < KS_ARCH_MAX; ++arch)
    check(ks_arch_supported((ks_arch)arch), "ks_arch_supported");
  check(!ks_arch_supported(KS_ARCH_MAX), "ks_arch_supported(KS_ARCH_MAX)");

  for (i = 0; i < sizeof vectors / sizeof vectors[0]; ++i) {
    const struct vector *v = &vectors[i];
    if (ks_open(v->arch, v->mode, &ks) != KS_ERR_OK) {
      fprintf(stderr, "FAIL: %s: ks_open\n", v->name);
      ++failures;
      continue;
    }
    expect(v->name, ks, v->text, 0, v->bytes, v->size, v->statements);
    check(ks_close(ks) == KS_ERR_OK, "ks_close");
  }

  /* x64_loop.py: the rel8 displacement depends on the address. */
  check(ks_open(KS_ARCH_X86, KS_MODE_64, &ks) == KS_ERR_OK, "ks_open x86-64");
  expect("x86-64 loop", ks, "loop 0x7ff6bf912aad", 0x7FF6BF912A84ULL,
         "\xe2\x27", 2, 1);
  ks_close(ks);

  /* test_symbol.py: a redefined label fails the whole call. */
  check(ks_open(KS_ARCH_X86, KS_MODE_32, &ks) == KS_ERR_OK, "ks_open x86-32");
  check(ks_asm(ks, "_label:; nop; _label:", 0, &encoding, &encoding_size,
               &count) == -1,
        "ks_asm with a redefined label");
  check(encoding == NULL && encoding_size == 0, "no encoding on failure");
  check(ks_errno(ks) == KS_ERR_ASM_SYMBOL_REDEFINED, "ks_errno");
  check(strcmp(ks_strerror(ks_errno(ks)),
               "Invalid symbol redefined (KS_ERR_ASM_SYMBOL_REDEFINED)") == 0,
        "ks_strerror(KS_ERR_ASM_SYMBOL_REDEFINED)");
  ks_close(ks);

  check(ks_open(KS_ARCH_MAX, KS_MODE_64, &ks) == KS_ERR_ARCH,
        "ks_open(KS_ARCH_MAX)");
  check(strcmp(ks_strerror(KS_ERR_ARCH),
               "Invalid/unsupported architecture (KS_ERR_ARCH)") == 0,
        "ks_strerror(KS_ERR_ARCH)");
  check(ks_open(KS_ARCH_X86, KS_MODE_64 + KS_MODE_BIG_ENDIAN, &ks) ==
            KS_ERR_MODE,
        "ks_open big-endian x86");
  check(ks_open(KS_ARCH_ARM64, KS_MODE_BIG_ENDIAN, &ks) == KS_ERR_MODE,
        "ks_open big-endian ARM64");
  check(ks_close(NULL) == KS_ERR_HANDLE, "ks_close(NULL)");

  if (failures == 0)
    puts("keystone C consumer: ok");
  return failures == 0 ? 0 : 1;
}
