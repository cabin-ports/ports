/* The helpers SpvHasResultAndType and Spv*ToString exist only with this
   macro, as in upstream's own tests (tests/CMakeLists.txt). */
#define SPV_ENABLE_UTILITY_CODE

#include <spirv/unified1/GLSL.std.450.h>
#include <spirv/unified1/NonSemanticDebugPrintf.h>
#include <spirv/unified1/OpenCL.std.h>
#include <spirv/unified1/spirv.h>

#include <stdio.h>
#include <string.h>

/* spirv.h defines its helpers as C99 inline definitions, which provide no
   external definition; declaring them extern makes this translation unit,
   the program's only one, provide it, so the calls below link whether or
   not they are inlined (C99 6.7.4p6-7). */
extern void SpvHasResultAndType(SpvOp opcode, bool* hasResult,
                                bool* hasResultType);
extern const char* SpvOpToString(SpvOp value);
extern const char* SpvCapabilityToString(SpvCapability value);
extern const char* SpvSourceLanguageToString(SpvSourceLanguage value);

static int failures = 0;

static void check(int ok, const char* what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void result_and_type(SpvOp op, bool result, bool type,
                            const char* what) {
  bool has_result = !result;
  bool has_type = !type;
  SpvHasResultAndType(op, &has_result, &has_type);
  check(has_result == result && has_type == type, what);
}

static unsigned int encode(SpvOp op, unsigned int word_count) {
  return (word_count << SpvWordCountShift) | (unsigned int)op;
}

int main(void) {
  /* A minimal GLCompute module, encoded as the SPIR-V specification lays it
     out (section 2.3): a five-word header, then instructions whose first
     word holds the word count in the high half and the opcode in the low. */
  unsigned int module[] = {
      SpvMagicNumber, SpvVersion, 0, 6, 0,
      encode(SpvOpCapability, 2), SpvCapabilityShader,
      encode(SpvOpMemoryModel, 3), SpvAddressingModelLogical,
      SpvMemoryModelGLSL450,
      /* "main" as a nul-terminated literal string: two words. */
      encode(SpvOpEntryPoint, 5), SpvExecutionModelGLCompute, 4, 0x6e69616d,
      0,
      encode(SpvOpExecutionMode, 6), 4, SpvExecutionModeLocalSize, 64, 1, 1,
      encode(SpvOpTypeVoid, 2), 1,
      encode(SpvOpTypeFunction, 3), 2, 1,
      encode(SpvOpFunction, 5), 1, 4, SpvFunctionControlMaskNone, 2,
      encode(SpvOpLabel, 2), 5,
      encode(SpvOpReturn, 1),
      encode(SpvOpFunctionEnd, 1),
  };
  /* The opcodes of those instructions, as the specification numbers them. */
  const unsigned int expected[] = {17, 14, 15, 16, 19, 33, 54, 248, 253, 56};
  const unsigned int words = sizeof module / sizeof module[0];
  const unsigned int count = sizeof expected / sizeof expected[0];
  unsigned int at = 5;
  unsigned int seen = 0;

  check(SpvMagicNumber == 0x07230203u, "SpvMagicNumber");
  check(SpvVersion == 0x00010600u && SPV_VERSION == 0x10600, "SPIR-V 1.6");
  check(SpvOpCodeMask == 0xffffu && SpvWordCountShift == 16, "word layout");

  /* Numbers the specification fixes, so a header that drifted from it fails. */
  check(SpvOpNop == 0 && SpvOpExtInstImport == 11 && SpvOpExtInst == 12,
        "opcode numbers");
  check(SpvOpCapability == 17 && SpvOpTypeInt == 21 && SpvOpStore == 62 &&
            SpvOpIAdd == 128 && SpvOpLabel == 248 && SpvOpReturn == 253,
        "more opcode numbers");
  check(SpvCapabilityShader == 1 && SpvCapabilityCooperativeMatrixKHR == 6022,
        "capability numbers");
  check(SpvDecorationBuiltIn == 11 && SpvBuiltInGlobalInvocationId == 28 &&
            SpvStorageClassStorageBuffer == 12,
        "decoration, built-in and storage class numbers");
  check((SpvMemoryAccessVolatileMask | SpvMemoryAccessAlignedMask) == 0x3,
        "memory access masks");
  check(SpvSourceLanguageSlang == 11 && SpvSourceLanguageApilaJai == 15,
        "source languages of this release");
  check(SpvExecutionModelGLCompute == 5 && SpvAddressingModelLogical == 0 &&
            SpvMemoryModelGLSL450 == 1 && SpvFunctionControlMaskNone == 0,
        "the module's operand numbers");

  /* Which instructions produce a result id and a result type. */
  result_and_type(SpvOpIAdd, true, true, "OpIAdd has both");
  result_and_type(SpvOpTypeInt, true, false, "OpTypeInt has a result only");
  result_and_type(SpvOpStore, false, false, "OpStore has neither");
  result_and_type((SpvOp)0xfffe, false, false, "unknown opcode");

  check(strcmp(SpvOpToString(SpvOpIAdd), "OpIAdd") == 0, "SpvOpToString");
  check(strcmp(SpvCapabilityToString(SpvCapabilityShader), "Shader") == 0,
        "SpvCapabilityToString");
  check(strcmp(SpvSourceLanguageToString(SpvSourceLanguageSlang), "Slang") ==
            0,
        "SpvSourceLanguageToString");
  check(strcmp(SpvSourceLanguageToString((SpvSourceLanguage)999),
               "Unknown") == 0,
        "unknown source language");

  /* The extended instruction sets, through their C names. */
  check(GLSLstd450Sin == 13 && GLSLstd450Count == 82, "GLSL.std.450");
  check(OpenCLstd_Native_cos == 81, "OpenCL.std");
  check(NonSemanticDebugPrintfDebugPrintf == 1, "NonSemantic.DebugPrintf");

  /* Walk the module back with the header's mask and shift. */
  check(module[0] == 0x07230203u && module[5] == 0x00020011u &&
            module[10] == 0x0005000fu,
        "encoded words");
  while (at < words && seen < count) {
    const unsigned int op = module[at] & SpvOpCodeMask;
    const unsigned int length = module[at] >> SpvWordCountShift;
    check(op == expected[seen], "decoded opcode");
    check(length > 0, "decoded word count");
    if (length == 0) {
      break;
    }
    if (op == SpvOpExecutionMode) {
      check(module[at + 2] == 17 &&
                module[at + 3] == 64,
            "decoded LocalSize");
    }
    at += length;
    ++seen;
  }
  check(seen == count && at == words, "decoded every instruction");

  if (failures != 0) {
    return 1;
  }
  printf("spirv.h: decoded %u instructions of a SPIR-V %u.%u module, from %s "
         "to %s\n",
         seen, (SpvVersion >> 16) & 0xff, (SpvVersion >> 8) & 0xff,
         SpvOpToString((SpvOp)expected[0]),
         SpvOpToString((SpvOp)expected[count - 1]));
  return 0;
}
