// Assembles x86-64 text with asmtk::AsmParser into an asmjit CodeHolder and
// checks the machine code, a label-resolved jump, an unknown-symbol handler
// and the rejection of invalid input.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <asmjit/x86.h>
#include <asmtk/asmtk.h>

using namespace asmjit;

static int fail(const char* what) {
  std::fprintf(stderr, "%s\n", what);
  return 1;
}

static bool bytes_are(const CodeHolder& code, const uint8_t* expected,
                      std::size_t size) {
  const CodeBuffer& buffer = code.text_section()->buffer();
  return buffer.size() == size &&
         std::memcmp(buffer.data(), expected, size) == 0;
}

static Error resolve_symbol(asmtk::AsmParser*, Operand* dst, const char* name,
                            std::size_t size) {
  if (size == 5 && std::memcmp(name, "Reg_A", 5) == 0) *dst = x86::rcx;
  return Error::kOk;
}

int main() {
  {
    CodeHolder code;
    if (code.init(Environment(Arch::kX64)) != Error::kOk) return fail("init");
    x86::Assembler a(&code);
    asmtk::AsmParser parser(&a);
    if (parser.parse("mov eax, 42\nret\n") != Error::kOk) return fail("parse");
    static const uint8_t expected[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    if (!bytes_are(code, expected, sizeof expected)) return fail("bytes");
  }

  {
    // A backward jump to a label defined in the text: jmp -2 is EB FE.
    CodeHolder code;
    if (code.init(Environment(Arch::kX64)) != Error::kOk) return fail("init2");
    x86::Assembler a(&code);
    asmtk::AsmParser parser(&a);
    if (parser.parse("loop:\njmp loop\n") != Error::kOk) return fail("parse2");
    static const uint8_t expected[] = {0xEB, 0xFE};
    if (!bytes_are(code, expected, sizeof expected)) return fail("label bytes");
  }

  {
    // The handler turns the unknown symbol Reg_A into rcx: mov rax, rcx.
    CodeHolder code;
    if (code.init(Environment(Arch::kX64)) != Error::kOk) return fail("init3");
    x86::Assembler a(&code);
    asmtk::AsmParser parser(&a);
    parser.set_unknown_symbol_handler(resolve_symbol);
    if (parser.parse("mov rax, Reg_A\n") != Error::kOk) return fail("parse3");
    static const uint8_t expected[] = {0x48, 0x89, 0xC8};
    if (!bytes_are(code, expected, sizeof expected)) return fail("handler");
  }

  {
    CodeHolder code;
    if (code.init(Environment(Arch::kX64)) != Error::kOk) return fail("init4");
    x86::Assembler a(&code);
    asmtk::AsmParser parser(&a);
    if (parser.parse("mov eax, ,\n") == Error::kOk) return fail("bad syntax");
    if (parser.parse("notaninstruction eax\n") == Error::kOk) {
      return fail("bad mnemonic");
    }
  }

  std::puts("ok");
  return 0;
}
