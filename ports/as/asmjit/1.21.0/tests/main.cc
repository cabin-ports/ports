// Encodes "mov eax, 42; ret" for x86-64 and "mov w0, 42; ret" for AArch64
// with both backends' assemblers and checks the bytes and the logged text.
// Then compiles a * b + c with the host's Compiler (virtual registers, the
// native calling convention), runs it as JIT code and checks the results.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <asmjit/a64.h>
#include <asmjit/host.h>
#include <asmjit/x86.h>

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

int main() {
  {
    CodeHolder code;
    StringLogger logger;
    if (code.init(Environment(Arch::kX64)) != Error::kOk) {
      return fail("x86-64 init");
    }
    code.set_logger(&logger);
    x86::Assembler a(&code);
    a.mov(x86::eax, 42);
    a.ret();
    static const uint8_t expected[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    if (!bytes_are(code, expected, sizeof expected)) return fail("x86-64 bytes");
    if (std::strstr(logger.data(), "mov eax, 42") == nullptr) {
      return fail("x86-64 text");
    }
  }

  {
    CodeHolder code;
    StringLogger logger;
    if (code.init(Environment(Arch::kAArch64)) != Error::kOk) {
      return fail("AArch64 init");
    }
    code.set_logger(&logger);
    a64::Assembler a(&code);
    a.mov(a64::w0, 42);
    a.ret(a64::x30);
    // movz w0, #42 (0x52800540) and ret (0xd65f03c0), little-endian.
    static const uint8_t expected[] = {0x40, 0x05, 0x80, 0x52,
                                       0xC0, 0x03, 0x5F, 0xD6};
    if (!bytes_are(code, expected, sizeof expected)) return fail("AArch64 bytes");
    if (std::strstr(logger.data(), "mov w0, 42") == nullptr) {
      return fail("AArch64 text");
    }
  }

#if ASMJIT_ARCH_X86 == 64 || ASMJIT_ARCH_ARM == 64
  using MulAdd = int (*)(int, int, int);
  JitRuntime rt;
  CodeHolder code;
  if (code.init(rt.environment(), rt.cpu_features()) != Error::kOk) {
    return fail("host init");
  }
  host::Compiler cc(&code);
  FuncNode* func = cc.add_func(FuncSignature::build<int, int, int, int>());
  host::Gp a = cc.new_gp32("a");
  host::Gp b = cc.new_gp32("b");
  host::Gp c = cc.new_gp32("c");
  func->set_arg(0, a);
  func->set_arg(1, b);
  func->set_arg(2, c);
#if ASMJIT_ARCH_X86
  cc.imul(a, b);
  cc.add(a, c);
#else
  cc.madd(a, a, b, c);
#endif
  cc.ret(a);
  cc.end_func();
  if (cc.finalize() != Error::kOk) return fail("finalize");

  MulAdd fn = nullptr;
  if (rt.add(&fn, &code) != Error::kOk) return fail("JitRuntime::add");
  const int r1 = fn(6, 7, -2);
  const int r2 = fn(-3, 5, 100);
  rt.release(fn);
  if (r1 != 40 || r2 != 85) {
    std::fprintf(stderr, "JIT results %d and %d\n", r1, r2);
    return 1;
  }
  std::printf("asmjit: x86-64 and AArch64 encodings and host JIT ok\n");
#else
  std::printf("asmjit: x86-64 and AArch64 encodings ok, no host JIT test\n");
#endif
  return 0;
}
