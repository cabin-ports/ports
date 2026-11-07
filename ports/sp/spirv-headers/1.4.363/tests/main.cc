// The helpers HasResultAndType and the *ToString functions exist only with
// this macro, as in upstream's own tests (tests/CMakeLists.txt).
#define SPV_ENABLE_UTILITY_CODE

#include <spirv/unified1/GLSL.std.450.h>
#include <spirv/unified1/OpenCL.std.h>
#include <spirv/unified1/spirv.hpp11>

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

bool same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

std::uint32_t encode(spv::Op op, std::uint32_t word_count) {
  return (word_count << spv::WordCountShift) | static_cast<std::uint32_t>(op);
}

void result_and_type(spv::Op op, bool result, bool type, const char* what) {
  bool has_result = !result;
  bool has_type = !type;
  spv::HasResultAndType(op, &has_result, &has_type);
  check(has_result == result && has_type == type, what);
}

}  // namespace

int main() {
  using spv::Op;

  check(spv::MagicNumber == 0x07230203u && spv::Version == 0x00010600u,
        "magic number and SPIR-V 1.6");
  check(spv::OpCodeMask == 0xffffu && spv::WordCountShift == 16,
        "word layout");
  check(static_cast<unsigned>(Op::OpIAdd) == 128 &&
            static_cast<unsigned>(Op::OpCapability) == 17,
        "opcode numbers");
  check(static_cast<unsigned>(spv::Capability::CooperativeMatrixKHR) == 6022,
        "capability number");

  // Which instructions produce a result id and a result type.
  result_and_type(Op::OpIAdd, true, true, "OpIAdd has both");
  result_and_type(Op::OpTypeInt, true, false, "OpTypeInt has a result only");
  result_and_type(Op::OpStore, false, false, "OpStore has neither");
  result_and_type(static_cast<Op>(0xfffe), false, false, "unknown opcode");

  check(same(spv::OpToString(Op::OpIAdd), "OpIAdd"), "OpToString");
  check(same(spv::CapabilityToString(spv::Capability::Shader), "Shader"),
        "CapabilityToString");
  check(same(spv::SourceLanguageToString(spv::SourceLanguage::Slang), "Slang"),
        "SourceLanguageToString");
  check(same(spv::SourceLanguageToString(static_cast<spv::SourceLanguage>(999)),
             "Unknown"),
        "SourceLanguageToString of an unknown value");

  // The scoped mask enums come with bitwise operators.
  constexpr spv::MemoryAccessMask access =
      spv::MemoryAccessMask::Volatile | spv::MemoryAccessMask::Aligned;
  static_assert(static_cast<unsigned>(access) == 0x3, "operator|");
  check((access & spv::MemoryAccessMask::Aligned) ==
            spv::MemoryAccessMask::Aligned,
        "operator&");
  check((access & ~spv::MemoryAccessMask::Volatile) ==
            spv::MemoryAccessMask::Aligned,
        "operator~");

  // The extended instruction sets, through their C++ names.
  check(GLSLstd450Sin == 13, "GLSL.std.450");
  check(OpenCLLIB::Native_cos == 81, "OpenCL.std");

  // Encode a minimal GLCompute module and list it back by opcode name.
  const std::vector<std::uint32_t> module = {
      spv::MagicNumber, spv::Version, 0, 6, 0,
      encode(Op::OpCapability, 2),
      static_cast<std::uint32_t>(spv::Capability::Shader),
      encode(Op::OpMemoryModel, 3),
      static_cast<std::uint32_t>(spv::AddressingModel::Logical),
      static_cast<std::uint32_t>(spv::MemoryModel::GLSL450),
      encode(Op::OpEntryPoint, 5),
      static_cast<std::uint32_t>(spv::ExecutionModel::GLCompute), 4,
      0x6e69616d, 0,  // "main", nul-terminated
      encode(Op::OpTypeVoid, 2), 1,
      encode(Op::OpTypeFunction, 3), 2, 1,
      encode(Op::OpFunction, 5), 1, 4,
      static_cast<std::uint32_t>(spv::FunctionControlMask::MaskNone), 2,
      encode(Op::OpLabel, 2), 5,
      encode(Op::OpReturn, 1),
      encode(Op::OpFunctionEnd, 1),
  };
  std::string listing;
  std::size_t at = 5;
  while (at < module.size()) {
    const std::uint32_t length = module[at] >> spv::WordCountShift;
    const Op op = static_cast<Op>(module[at] & spv::OpCodeMask);
    if (length == 0) {
      check(false, "decoded word count");
      break;
    }
    listing += spv::OpToString(op);
    listing += ' ';
    at += length;
  }
  check(at == module.size(), "decoded every word");
  check(listing ==
            "OpCapability OpMemoryModel OpEntryPoint OpTypeVoid OpTypeFunction "
            "OpFunction OpLabel OpReturn OpFunctionEnd ",
        "decoded listing");

  if (failures != 0) {
    return 1;
  }
  std::printf("spirv.hpp11: %s\n", listing.c_str());
  return 0;
}
