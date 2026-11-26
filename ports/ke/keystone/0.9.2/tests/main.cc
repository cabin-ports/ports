// Exercises keystone's options from C++: the AT&T and NASM syntaxes, a
// symbol resolver, an invalid option, and a missing symbol.  Expected bytes
// come from upstream's suite/regress (test_all_archs.py:32 and :41,
// x64_nasm_default_rel.py:20-21, x64_sym_resolver.py:30-31,
// x86_call_ptr_sym.py:27-37 and test_symbol.py:27-29).
#include <keystone/keystone.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Assembles text at address, or returns false with the encoding left empty.
bool assemble(ks_engine* ks, const char* text, std::uint64_t address,
              std::vector<unsigned char>& bytes, std::size_t& statements) {
  unsigned char* encoding = nullptr;
  std::size_t size = 0;
  statements = 0;
  bytes.clear();
  if (ks_asm(ks, text, address, &encoding, &size, &statements) != 0)
    return false;
  bytes.assign(encoding, encoding + size);
  ks_free(encoding);
  return true;
}

void expect(const char* name, ks_engine* ks, const char* text,
            std::uint64_t address, const std::vector<unsigned char>& want,
            std::size_t want_statements = 1) {
  std::vector<unsigned char> got;
  std::size_t statements = 0;
  if (!assemble(ks, text, address, got, statements)) {
    std::fprintf(stderr, "FAIL: %s: %s\n", name, ks_strerror(ks_errno(ks)));
    ++failures;
    return;
  }
  if (got != want) {
    std::string hex;
    char byte[4];
    for (unsigned char b : got) {
      std::snprintf(byte, sizeof byte, " %02x", b);
      hex += byte;
    }
    std::fprintf(stderr, "FAIL: %s: encoded%s\n", name, hex.c_str());
    ++failures;
  }
  if (statements != want_statements) {
    std::fprintf(stderr, "FAIL: %s: %lu statements\n", name,
                 static_cast<unsigned long>(statements));
    ++failures;
  }
}

int resolver_calls = 0;

bool resolve(const char* symbol, std::uint64_t* value) {
  ++resolver_calls;
  if (std::strcmp(symbol, "GetPhoneBuildString") != 0) return false;
  *value = 0x41b000;
  return true;
}

}  // namespace

int main() {
  ks_engine* ks = nullptr;

  check(ks_open(KS_ARCH_X86, KS_MODE_64, &ks) == KS_ERR_OK, "ks_open x86-64");
  check(ks_option(ks, KS_OPT_SYNTAX, KS_OPT_SYNTAX_ATT) == KS_ERR_OK,
        "KS_OPT_SYNTAX_ATT");
  expect("AT&T add", ks, "add %rcx, %rax", 0, {0x48, 0x01, 0xc8});
  check(ks_option(ks, KS_OPT_SYNTAX, KS_OPT_SYNTAX_NASM) == KS_ERR_OK,
        "KS_OPT_SYNTAX_NASM");
  // Four statements: "default" leaves its line end unconsumed, which then
  // counts as a blank one (AsmParser.cpp:1451-1454, 5314-5316).
  expect("NASM default rel", ks, "default rel\nlea rax, [__data]\n__data:", 0,
         {0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00}, 4);
  check(ks_option(ks, KS_OPT_SYNTAX, 12345) == KS_ERR_OPT_INVALID,
        "an unknown syntax");
  ks_close(ks);

  // CALL rel32 (E8) to 0x7FF98A050840 from 0x7FF98A081A38: -0x311FD.
  check(ks_open(KS_ARCH_X86, KS_MODE_64, &ks) == KS_ERR_OK, "ks_open x86-64");
  expect("call to an address", ks, "call 0x7FF98A050840", 0x7FF98A081A38ULL,
         {0xe8, 0x03, 0xee, 0xfc, 0xff});
  ks_close(ks);

  // CALL r/m32 (FF /2) through the absolute address the resolver supplies,
  // in NASM, Intel and AT&T syntax.
  const std::vector<unsigned char> call = {0xff, 0x15, 0x00, 0xb0, 0x41, 0x00};
  const struct {
    int syntax;
    const char* text;
  } calls[] = {{KS_OPT_SYNTAX_NASM, "call [GetPhoneBuildString]"},
               {KS_OPT_SYNTAX_INTEL, "call [GetPhoneBuildString]"},
               {KS_OPT_SYNTAX_ATT, "call *GetPhoneBuildString"}};
  for (const auto& c : calls) {
    check(ks_open(KS_ARCH_X86, KS_MODE_32, &ks) == KS_ERR_OK, "ks_open x86-32");
    check(ks_option(ks, KS_OPT_SYNTAX, c.syntax) == KS_ERR_OK, "KS_OPT_SYNTAX");
    check(ks_option(ks, KS_OPT_SYM_RESOLVER,
                    reinterpret_cast<std::size_t>(&resolve)) == KS_ERR_OK,
          "KS_OPT_SYM_RESOLVER");
    expect(c.text, ks, c.text, 0, call);
    ks_close(ks);
  }
  check(resolver_calls >= 3, "the resolver is consulted");

  // test_symbol.py: eflags is no Intel-syntax operand, so it is a symbol.
  check(ks_open(KS_ARCH_X86, KS_MODE_32, &ks) == KS_ERR_OK, "ks_open x86-32");
  std::vector<unsigned char> bytes;
  std::size_t statements = 0;
  check(!assemble(ks, "mov eax, eflags", 0, bytes, statements),
        "a missing symbol fails");
  check(ks_errno(ks) == KS_ERR_ASM_SYMBOL_MISSING, "ks_errno");
  check(std::string(ks_strerror(ks_errno(ks))) ==
            "Cannot find a symbol (KS_ERR_ASM_SYMBOL_MISSING)",
        "ks_strerror(KS_ERR_ASM_SYMBOL_MISSING)");
  ks_close(ks);

  check(ks_open(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN, &ks) == KS_ERR_OK,
        "ks_open ARM64");
  expect("ARM64 ldr", ks, "ldr w1, [sp, #0x8]", 0, {0xe1, 0x0b, 0x40, 0xb9});
  check(ks_option(ks, KS_OPT_SYNTAX, KS_OPT_SYNTAX_ATT) == KS_ERR_OPT_INVALID,
        "KS_OPT_SYNTAX outside x86");
  check(ks_close(ks) == KS_ERR_OK, "ks_close");

  check(std::string(ks_strerror(KS_ERR_OK)) == "OK (KS_ERR_OK)",
        "ks_strerror(KS_ERR_OK)");

  if (failures == 0) std::puts("keystone C++ consumer: ok");
  return failures == 0 ? 0 : 1;
}
