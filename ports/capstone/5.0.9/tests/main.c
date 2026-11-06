/* Disassembles one known instruction on each of the 18 architectures the
 * default build carries.  Expected text comes from upstream's own test
 * vectors (suite/MC/<arch>/<file>:<line>, suite/cstest/issues.cs:<line>),
 * except where upstream has none: XCore follows LLVM's
 * test/MC/Disassembler/XCore/xcore.txt, which Capstone's XCore tables come
 * from, and EVM, WASM, MOS65XX and M680X follow their printers. */
#include <capstone/capstone.h>

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
  cs_arch arch;
  unsigned mode;
  size_t syntax; /* 0 leaves the default */
  const char *bytes;
  size_t size;
  const char *text; /* mnemonic, a space, op_str */
  int detail;       /* CS_OPT_DETAIL, as the vector was recorded */
};

static const struct vector vectors[] = {
    /* ARM/basic-arm-instructions.s.cs:105 */
    {"ARM", CS_ARCH_ARM, CS_MODE_ARM, 0, "\x04\x20\xa0\xe1", 4, "mov r2, r4",
     0},
    /* ARM/basic-thumb-instructions.s.cs:2 */
    {"Thumb", CS_ARCH_ARM, CS_MODE_THUMB, 0, "\x74\x41", 2, "adcs r4, r6", 0},
    /* AArch64/basic-a64-instructions.s.cs:2 */
    {"AArch64", CS_ARCH_ARM64, CS_MODE_ARM, 0, "\x82\x00\x25\x8b", 4,
     "add x2, x4, w5, uxtb", 0},
    /* Mips/mips-alu-instructions.s.cs:2 */
    {"MIPS32", CS_ARCH_MIPS, CS_MODE_MIPS32, 0, "\x24\x48\xc7\x00", 4,
     "and $t1, $a2, $a3", 0},
    /* Mips/mips-control-instructions-64.s.cs:2 */
    {"MIPS64 big-endian", CS_ARCH_MIPS, CS_MODE_MIPS64 | CS_MODE_BIG_ENDIAN, 0,
     "\x00\x00\x00\x0d", 4, "break", 0},
    /* X86/intel-syntax-encoding.s.cs:29 */
    {"x86-64", CS_ARCH_X86, CS_MODE_64, 0, "\xc2\x08\x00", 3, "ret 8", 0},
    /* X86/x86_64-bmi-encoding.s.cs:3 */
    {"x86-64 AT&T", CS_ARCH_X86, CS_MODE_64, CS_OPT_SYNTAX_ATT,
     "\xc4\xc2\xa8\xf3\xd3", 5, "blsmskq %r11, %r10", 0},
    /* PowerPC/ppc64-operands.s.cs:2 */
    {"PowerPC", CS_ARCH_PPC, CS_MODE_BIG_ENDIAN, CS_OPT_SYNTAX_NOREGNAME,
     "\x7c\x22\x1a\x14", 4, "add 1, 2, 3", 0},
    /* Sparc/sparc-alu-instructions.s.cs:2 */
    {"SPARC", CS_ARCH_SPARC, CS_MODE_BIG_ENDIAN, 0, "\x80\x00\x00\x00", 4,
     "add %g0, %g0, %g0", 0},
    /* SystemZ/insn-good.s.cs:74 */
    {"SystemZ", CS_ARCH_SYSZ, 0, 0, "\xb9\x08\x00\x0f", 4, "agr %r0, %r15", 0},
    /* LLVM xcore.txt "get r11, ed"; the mode tests/test_xcore.c uses */
    {"XCore", CS_ARCH_XCORE, CS_MODE_BIG_ENDIAN, 0, "\xfe\x0f", 2,
     "get r11, ed", 0},
    /* suite/cstest/issues.cs:254 */
    {"M68K", CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040, 0,
     "\x4e\x75", 2, "rts", 0},
    /* suite/cstest/issues.cs:252 */
    {"M68K FPU", CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040, 0,
     "\xf2\x27\x74\x00", 4, "fmove.d fp0, -(a7)", 0},
    /* suite/cstest/issues.cs:1072; TMS320C64xInstPrinter.c:44 prints the
     * unit and the parallel bar only with CS_OPT_DETAIL */
    {"TMS320C64x", CS_ARCH_TMS320C64X, CS_MODE_LITTLE_ENDIAN, 0,
     "\xa9\xff\x7f\x02", 4, "mvk.S1 -1, a4 ||", 1},
    /* 6809 LDA immediate; M680XInstPrinter.c prints "#%d" */
    {"M680X", CS_ARCH_M680X, CS_MODE_M680X_6809, 0, "\x86\x2a", 2, "lda #42",
     0},
    /* EVM PUSH1; EVMInstPrinter.c prints the pushed bytes as "%02x" */
    {"EVM", CS_ARCH_EVM, 0, 0, "\x60\x80", 2, "push1 80", 0},
    /* i32.const; WASMInstPrinter.c prints a varuint32 as "0x%x" */
    {"WASM", CS_ARCH_WASM, 0, 0, "\x41\x2a", 2, "i32.const 0x2a", 0},
    /* 6502 LDA immediate; the default hex prefix is "0x" */
    {"MOS65XX", CS_ARCH_MOS65XX, CS_MODE_MOS65XX_6502, 0, "\xa9\x2a", 2,
     "lda #0x2a", 0},
    /* BPF/extended-all.cs:5 */
    {"eBPF", CS_ARCH_BPF, CS_MODE_LITTLE_ENDIAN | CS_MODE_BPF_EXTENDED, 0,
     "\x0c\x42\x0a\x48\x58\xc4\xef\x37", 8, "add r2, r4", 0},
    /* RISCV/insn-riscv32.s.cs:2 */
    {"RISC-V", CS_ARCH_RISCV, CS_MODE_RISCV32, 0, "\x37\x34\x00\x00", 4,
     "lui s0, 3", 0},
    /* suite/cstest/issues.cs:1068 */
    {"SH-2A", CS_ARCH_SH, CS_MODE_SH2A | CS_MODE_BIG_ENDIAN, 0,
     "\x32\x11\x92\x00", 4, "movu.w @(1024,r1),r2", 1},
    /* TriCore/tc161.s.cs:2 */
    {"TriCore", CS_ARCH_TRICORE, CS_MODE_TRICORE_161, 0, "\x0b\x00\xc0\x01", 4,
     "abs d0, d0", 0},
};

static void disassemble(const struct vector *v) {
  csh handle;
  cs_insn *insn = NULL;
  size_t count;
  char text[sizeof insn->mnemonic + sizeof insn->op_str + 1];
  cs_err err = cs_open(v->arch, (cs_mode)v->mode, &handle);

  if (err != CS_ERR_OK) {
    fprintf(stderr, "FAIL: %s: cs_open: %s\n", v->name, cs_strerror(err));
    ++failures;
    return;
  }
  if (v->syntax != 0)
    check(cs_option(handle, CS_OPT_SYNTAX, v->syntax) == CS_ERR_OK, v->name);
  if (v->detail)
    check(cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON) == CS_ERR_OK, v->name);
  count = cs_disasm(handle, (const uint8_t *)v->bytes, v->size, 0x1000, 0,
                    &insn);
  if (count != 1) {
    fprintf(stderr, "FAIL: %s: %u instructions\n", v->name, (unsigned)count);
    ++failures;
  } else {
    char *tab;
    snprintf(text, sizeof text, "%s%s%s", insn->mnemonic,
             insn->op_str[0] ? " " : "", insn->op_str);
    /* Tabs read as spaces, as upstream's cstest compares them. */
    while ((tab = strchr(text, '\t')) != NULL) *tab = ' ';
    if (strcmp(text, v->text) != 0 || insn->size != v->size ||
        insn->address != 0x1000 || memcmp(insn->bytes, v->bytes, v->size)) {
      fprintf(stderr, "FAIL: %s: \"%s\" (%u bytes), want \"%s\"\n", v->name,
              text, (unsigned)insn->size, v->text);
      ++failures;
    }
  }
  cs_free(insn, count);
  cs_close(&handle);
}

int main(void) {
  int major = 0, minor = 0;
  unsigned version = cs_version(&major, &minor);
  size_t i;
  int arch;
  csh handle;

  check(major == 5 && minor == 0 && version == CS_MAKE_VERSION(5, 0),
        "cs_version");
  check(CS_API_MAJOR == 5 && CS_API_MINOR == 0 && CS_VERSION_EXTRA == 9,
        "header version 5.0.9");
  check(cs_support(CS_ARCH_ALL), "every architecture compiled in");
  for (arch = CS_ARCH_ARM; arch < CS_ARCH_MAX; ++arch)
    if (!cs_support(arch)) {
      fprintf(stderr, "FAIL: architecture %d missing\n", arch);
      ++failures;
    }
  check(!cs_support(CS_SUPPORT_DIET), "not the diet build");
  check(!cs_support(CS_SUPPORT_X86_REDUCE), "not the reduced x86 build");

  check(cs_open(CS_ARCH_MAX, CS_MODE_LITTLE_ENDIAN, &handle) == CS_ERR_ARCH,
        "an unknown architecture is CS_ERR_ARCH");
  check(cs_open(CS_ARCH_X86, CS_MODE_BIG_ENDIAN, &handle) == CS_ERR_MODE,
        "big-endian x86 is CS_ERR_MODE");
  check(strcmp(cs_strerror(CS_ERR_OK), "OK (CS_ERR_OK)") == 0,
        "cs_strerror");

  for (i = 0; i < sizeof vectors / sizeof vectors[0]; ++i)
    disassemble(&vectors[i]);

  if (failures != 0) return 1;
  puts("capstone C consumer ok");
  return 0;
}
