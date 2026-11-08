// C++ consumer: TShader and TProgram on a GLSL compute shader that pulls in
// a declaration through a user Includer, reflection, GlslangToSpv and the
// disassembler, the HLSL front end, and a malformed shader's error.
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/SPIRV/disassemble.h>
#include <glslang/SPIRV/spirv.hpp11>
#include <glslang/build_info.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static_assert(GLSLANG_VERSION_MAJOR == 16 && GLSLANG_VERSION_MINOR == 4 && GLSLANG_VERSION_PATCH == 0,
              "build_info.h names 16.4.0");
static_assert(GLSLANG_VERSION_GREATER_OR_EQUAL_TO(16, 4, 0) && GLSLANG_VERSION_LESS_THAN(16, 4, 1),
              "build_info.h's comparison macros");

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

class Includer : public glslang::TShader::Includer {
 public:
  int served = 0;
  int released = 0;

  IncludeResult* includeLocal(const char* header_name, const char*, size_t) override {
    if (std::strcmp(header_name, "scale.glsl") != 0) {
      return nullptr;
    }
    ++served;
    static const char text[] = "const float kScale = 3.0;\n";
    return new IncludeResult(header_name, text, sizeof text - 1, nullptr);
  }

  void releaseInclude(IncludeResult* result) override {
    ++released;
    delete result;
  }
};

const char* const kGlsl =
    "#version 450\n"
    "#extension GL_GOOGLE_include_directive : require\n"
    "#include \"scale.glsl\"\n"
    "layout(local_size_x = 8, local_size_y = 4) in;\n"
    "layout(std430, binding = 0) buffer Data { float values[]; } data;\n"
    "void main() {\n"
    "  uint i = gl_GlobalInvocationID.x;\n"
    "  data.values[i] = data.values[i] * kScale;\n"
    "}\n";

const char* const kHlsl =
    "RWStructuredBuffer<float> values : register(u0);\n"
    "[numthreads(8, 4, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) {\n"
    "  values[id.x] = values[id.x] * 3.0;\n"
    "}\n";

void set_vulkan_environment(glslang::TShader& shader, glslang::EShSource source) {
  shader.setEnvInput(source, EShLangCompute, glslang::EShClientVulkan, 100);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
}

std::string disassemble(const std::vector<unsigned int>& spirv) {
  std::ostringstream out;
  spv::Disassemble(out, spirv);
  return out.str();
}

bool contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  const glslang::Version version = glslang::GetVersion();
  CHECK(version.major == 16 && version.minor == 4 && version.patch == 0);
  CHECK(std::strcmp(version.flavor, "") == 0);

  CHECK(glslang::InitializeProcess());
  const TBuiltInResource* resources = GetDefaultResources();
  CHECK(resources->maxComputeWorkGroupSizeX == 1024);

  {
    glslang::TShader shader(EShLangCompute);
    shader.setStrings(&kGlsl, 1);
    set_vulkan_environment(shader, glslang::EShSourceGlsl);
    Includer includer;
    const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    CHECK(shader.parse(resources, 100, false, messages, includer));
    CHECK(includer.served == 1);
    CHECK(includer.released == includer.served);

    glslang::TProgram program;
    program.addShader(&shader);
    CHECK(program.link(messages));
    // Storage blocks are listed apart from uniform blocks only with
    // EShReflectionSeparateBuffers.
    CHECK(program.buildReflection(EShReflectionDefault | EShReflectionSeparateBuffers));
    CHECK(program.getNumBufferBlocks() == 1);
    if (program.getNumBufferBlocks() == 1) {
      CHECK(program.getBufferBlock(0).name == "Data");
    }

    std::vector<unsigned int> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), spirv);
    CHECK(spirv.size() > 5);
    if (spirv.size() > 5) {
      CHECK(spirv[0] == spv::MagicNumber);
      CHECK(spirv[1] == 0x00010500u);
    }
    const std::string text = disassemble(spirv);
    CHECK(contains(text, "EntryPoint GLCompute"));
    CHECK(contains(text, "LocalSize 8 4 1"));
    // The included kScale = 3.0; the disassembler prints a constant's raw
    // word, 0x40400000 for 3.0f.
    CHECK(contains(text, "Constant 1077936128"));
  }

  {
    glslang::TShader shader(EShLangCompute);
    shader.setStrings(&kHlsl, 1);
    shader.setEntryPoint("main");
    set_vulkan_environment(shader, glslang::EShSourceHlsl);
    const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    CHECK(shader.parse(resources, 100, false, messages));

    glslang::TProgram program;
    program.addShader(&shader);
    CHECK(program.link(messages));
    std::vector<unsigned int> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), spirv);
    const std::string text = disassemble(spirv);
    CHECK(contains(text, "EntryPoint GLCompute"));
    CHECK(contains(text, "LocalSize 8 4 1"));
  }

  {
    const char* const broken = "#version 450\nvoid main() { undeclared_name = 1; }\n";
    glslang::TShader shader(EShLangCompute);
    shader.setStrings(&broken, 1);
    set_vulkan_environment(shader, glslang::EShSourceGlsl);
    CHECK(!shader.parse(resources, 100, false, EShMsgDefault));
    CHECK(contains(shader.getInfoLog(), "'undeclared_name' : undeclared identifier"));
  }

  glslang::FinalizeProcess();

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("glslang C++ consumer test passed");
  return 0;
}
