// Exercises Capstone's detail API on x86-64 (operands, registers read and
// written, groups), the AT&T printer, the iterator, SKIPDATA, a custom
// mnemonic, per-architecture details for EVM, WASM and MOS65XX, and a
// caller-installed allocator (CS_OPT_MEM) that sees every allocation.
#include <capstone/capstone.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Allocations the library holds; zero again once every handle and
// instruction array is released.
long live = 0;
long calls = 0;

void* CAPSTONE_API counted_malloc(size_t size) {
  ++calls;
  ++live;
  return std::malloc(size);
}

void* CAPSTONE_API counted_calloc(size_t n, size_t size) {
  ++calls;
  ++live;
  return std::calloc(n, size);
}

void* CAPSTONE_API counted_realloc(void* p, size_t size) {
  ++calls;
  if (p == nullptr) ++live;
  return std::realloc(p, size);
}

void CAPSTONE_API counted_free(void* p) {
  if (p != nullptr) --live;
  std::free(p);
}

int CAPSTONE_API counted_vsnprintf(char* s, size_t n, const char* format,
                                   va_list args) {
  return std::vsnprintf(s, n, format, args);
}

std::string text(const cs_insn& insn) {
  std::string s = insn.mnemonic;
  if (insn.op_str[0] != '\0') s += std::string(" ") + insn.op_str;
  return s;
}

const uint8_t kX86[] = {0x55, 0x48, 0x8b, 0x05, 0xb8, 0x13, 0x00, 0x00, 0xc3};

bool contains(const cs_regs regs, uint8_t count, unsigned reg) {
  for (uint8_t i = 0; i < count; ++i)
    if (regs[i] == reg) return true;
  return false;
}

void x86_detail() {
  csh h;
  if (cs_open(CS_ARCH_X86, CS_MODE_64, &h) != CS_ERR_OK) {
    check(false, "cs_open x86-64");
    return;
  }
  cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
  cs_insn* insn = nullptr;
  size_t count = cs_disasm(h, kX86, sizeof kX86, 0x1000, 0, &insn);
  check(count == 3, "three x86-64 instructions");
  if (count == 3) {
    check(text(insn[0]) == "push rbp" && insn[0].id == X86_INS_PUSH &&
              insn[0].address == 0x1000 && insn[0].size == 1,
          "push rbp");
    check(text(insn[1]) == "mov rax, qword ptr [rip + 0x13b8]" &&
              insn[1].address == 0x1001 && insn[1].size == 7,
          "mov rax, qword ptr [rip + 0x13b8]");
    check(text(insn[2]) == "ret" && insn[2].address == 0x1008, "ret");

    const cs_x86& mov = insn[1].detail->x86;
    check(mov.op_count == 2, "mov has two operands");
    check(mov.operands[0].type == X86_OP_REG &&
              mov.operands[0].reg == X86_REG_RAX &&
              mov.operands[0].access == CS_AC_WRITE,
          "mov writes rax");
    check(mov.operands[1].type == X86_OP_MEM &&
              mov.operands[1].mem.base == X86_REG_RIP &&
              mov.operands[1].mem.index == X86_REG_INVALID &&
              mov.operands[1].mem.disp == 0x13b8 &&
              mov.operands[1].size == 8 &&
              mov.operands[1].access == CS_AC_READ,
          "mov reads qword [rip + 0x13b8]");

    cs_regs read, written;
    uint8_t nread = 0, nwritten = 0;
    check(cs_regs_access(h, &insn[0], read, &nread, written, &nwritten) ==
                  CS_ERR_OK &&
              contains(read, nread, X86_REG_RBP) &&
              contains(read, nread, X86_REG_RSP) &&
              contains(written, nwritten, X86_REG_RSP),
          "push reads rbp and rsp, writes rsp");
    check(std::strcmp(cs_reg_name(h, X86_REG_RIP), "rip") == 0,
          "cs_reg_name");
    check(cs_insn_group(h, &insn[2], CS_GRP_RET) &&
              !cs_insn_group(h, &insn[1], CS_GRP_RET),
          "only ret is in CS_GRP_RET");
  }
  cs_free(insn, count);

  // The AT&T printer, which the diet build and CAPSTONE_X86_ATT_DISABLE
  // leave out.
  cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
  count = cs_disasm(h, kX86, sizeof kX86, 0x1000, 2, &insn);
  check(count == 2 && text(insn[0]) == "pushq %rbp" &&
            text(insn[1]) == "movq 0x13b8(%rip), %rax",
        "AT&T syntax");
  cs_free(insn, count);
  cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);

  // One instruction at a time into a caller-held cs_insn.
  cs_insn* one = cs_malloc(h);
  const uint8_t* code = kX86;
  size_t size = sizeof kX86;
  uint64_t address = 0x1000;
  std::string all;
  while (cs_disasm_iter(h, &code, &size, &address, one))
    all += text(*one) + "; ";
  check(all == "push rbp; mov rax, qword ptr [rip + 0x13b8]; ret; " &&
            size == 0 && address == 0x1000 + sizeof kX86,
        "cs_disasm_iter walks the buffer");
  cs_free(one, 1);

  // A custom mnemonic, then the default again.
  cs_opt_mnem mnem = {X86_INS_PUSH, "PUSHQ"};
  cs_option(h, CS_OPT_MNEMONIC, reinterpret_cast<size_t>(&mnem));
  count = cs_disasm(h, kX86, sizeof kX86, 0x1000, 1, &insn);
  check(count == 1 && text(insn[0]) == "PUSHQ rbp", "CS_OPT_MNEMONIC");
  cs_free(insn, count);
  mnem.mnemonic = nullptr;
  cs_option(h, CS_OPT_MNEMONIC, reinterpret_cast<size_t>(&mnem));
  count = cs_disasm(h, kX86, sizeof kX86, 0x1000, 1, &insn);
  check(count == 1 && text(insn[0]) == "push rbp", "mnemonic restored");
  cs_free(insn, count);
  cs_close(&h);
}

// 0x06 (push es) does not exist in 64-bit mode.
void skipdata() {
  const uint8_t code[] = {0x55, 0x06, 0xc3};
  csh h;
  if (cs_open(CS_ARCH_X86, CS_MODE_64, &h) != CS_ERR_OK) {
    check(false, "cs_open x86-64");
    return;
  }
  cs_insn* insn = nullptr;
  size_t count = cs_disasm(h, code, sizeof code, 0, 0, &insn);
  check(count == 1, "disassembly stops at the invalid byte");
  cs_free(insn, count);

  cs_option(h, CS_OPT_SKIPDATA, CS_OPT_ON);
  count = cs_disasm(h, code, sizeof code, 0, 0, &insn);
  check(count == 3 && text(insn[1]) == ".byte 0x06" && text(insn[2]) == "ret",
        "SKIPDATA steps over it");
  cs_free(insn, count);

  cs_opt_skipdata setup = {"db", nullptr, nullptr};
  cs_option(h, CS_OPT_SKIPDATA_SETUP, reinterpret_cast<size_t>(&setup));
  count = cs_disasm(h, code, sizeof code, 0, 0, &insn);
  check(count == 3 && text(insn[1]) == "db 0x06", "SKIPDATA mnemonic");
  cs_free(insn, count);
  cs_close(&h);
}

// Details that only these architectures carry.
void other_details() {
  csh h;
  cs_insn* insn = nullptr;
  size_t count;

  if (cs_open(CS_ARCH_EVM, CS_MODE_LITTLE_ENDIAN, &h) == CS_ERR_OK) {
    const uint8_t push1[] = {0x60, 0x80};
    cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
    count = cs_disasm(h, push1, sizeof push1, 0, 0, &insn);
    // PUSH1 pops nothing, pushes one word and costs 3 gas.
    check(count == 1 && insn[0].id == EVM_INS_PUSH1 &&
              insn[0].detail->evm.pop == 0 && insn[0].detail->evm.push == 1 &&
              insn[0].detail->evm.fee == 3,
          "EVM PUSH1 detail");
    cs_free(insn, count);
    cs_close(&h);
  } else {
    check(false, "cs_open EVM");
  }

  if (cs_open(CS_ARCH_WASM, CS_MODE_LITTLE_ENDIAN, &h) == CS_ERR_OK) {
    // i32.const with the LEB128 immediate 624485 (0xe5 0x8e 0x26).
    const uint8_t i32_const[] = {0x41, 0xe5, 0x8e, 0x26};
    cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
    count = cs_disasm(h, i32_const, sizeof i32_const, 0, 0, &insn);
    check(count == 1 && insn[0].id == WASM_INS_I32_CONST &&
              insn[0].size == 4 && insn[0].detail->wasm.op_count == 1 &&
              insn[0].detail->wasm.operands[0].varuint32 == 624485,
          "WASM LEB128 immediate");
    cs_free(insn, count);
    cs_close(&h);
  } else {
    check(false, "cs_open WASM");
  }

  if (cs_open(CS_ARCH_MOS65XX, CS_MODE_MOS65XX_6502, &h) == CS_ERR_OK) {
    const uint8_t lda[] = {0xa9, 0x2a};
    cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
    cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_MOTOROLA);
    count = cs_disasm(h, lda, sizeof lda, 0, 0, &insn);
    check(count == 1 && text(insn[0]) == "lda #$2a" &&
              insn[0].detail->mos65xx.op_count == 1 &&
              insn[0].detail->mos65xx.operands[0].type == MOS65XX_OP_IMM &&
              insn[0].detail->mos65xx.operands[0].imm == 0x2a,
          "MOS65XX Motorola syntax and detail");
    cs_free(insn, count);
    cs_close(&h);
  } else {
    check(false, "cs_open MOS65XX");
  }
}

}  // namespace

int main() {
  // Before any other call, as capstone.h requires of CS_OPT_MEM.
  cs_opt_mem mem = {counted_malloc, counted_calloc, counted_realloc,
                    counted_free, counted_vsnprintf};
  check(cs_option(0, CS_OPT_MEM, reinterpret_cast<size_t>(&mem)) == CS_ERR_OK,
        "CS_OPT_MEM");

  x86_detail();
  skipdata();
  other_details();

  check(calls > 0 && live == 0,
        "every allocation went through CS_OPT_MEM and was released");
  if (failures != 0) return 1;
  std::puts("capstone C++ consumer ok");
  return 0;
}
