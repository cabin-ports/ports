// Through the C++ API, a fragment shader hand-assembled below as SPIR-V words
// reflects its stage interface and uniform block, keeps a renamed input
// (spirv_cross_util), cross-compiles to ES GLSL with Vulkan semantics, HLSL,
// MSL, C++ and JSON reflection, and a module with a bad header throws
// spirv_cross::CompilerError.
#include <spirv_cpp.hpp>
#include <spirv_cross.hpp>
#include <spirv_cross_util.hpp>
#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>
#include <spirv_parser.hpp>
#include <spirv_reflect.hpp>

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

// #version 450
// layout(location = 0) in vec4 color_in;
// layout(location = 0) out vec4 frag_out;
// layout(set = 2, binding = 3) uniform Params { float scale; } params;
// void main() { frag_out = color_in * params.scale; }
// as SPIR-V 1.0.
const std::uint32_t shader[] = {
  0x07230203, 0x00010000, 0x00000000, 0x00000015, 0x00000000, 0x00020011,
  0x00000001, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
  0x00000001, 0x6e69616d, 0x00000000, 0x00000002, 0x00000003, 0x00030010,
  0x00000001, 0x00000007, 0x00040005, 0x00000001, 0x6e69616d, 0x00000000,
  0x00050005, 0x00000002, 0x6f6c6f63, 0x6e695f72, 0x00000000, 0x00050005,
  0x00000003, 0x67617266, 0x74756f5f, 0x00000000, 0x00040005, 0x0000000a,
  0x61726150, 0x0000736d, 0x00050006, 0x0000000a, 0x00000000, 0x6c616373,
  0x00000065, 0x00040005, 0x0000000c, 0x61726170, 0x0000736d, 0x00040047,
  0x00000002, 0x0000001e, 0x00000000, 0x00040047, 0x00000003, 0x0000001e,
  0x00000000, 0x00050048, 0x0000000a, 0x00000000, 0x00000023, 0x00000000,
  0x00030047, 0x0000000a, 0x00000002, 0x00040047, 0x0000000c, 0x00000022,
  0x00000002, 0x00040047, 0x0000000c, 0x00000021, 0x00000003, 0x00020013,
  0x00000004, 0x00030021, 0x00000005, 0x00000004, 0x00030016, 0x00000006,
  0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020,
  0x00000008, 0x00000001, 0x00000007, 0x00040020, 0x00000009, 0x00000003,
  0x00000007, 0x0003001e, 0x0000000a, 0x00000006, 0x00040020, 0x0000000b,
  0x00000002, 0x0000000a, 0x00040015, 0x0000000d, 0x00000020, 0x00000001,
  0x0004002b, 0x0000000d, 0x0000000e, 0x00000000, 0x00040020, 0x0000000f,
  0x00000002, 0x00000006, 0x0004003b, 0x00000008, 0x00000002, 0x00000001,
  0x0004003b, 0x00000009, 0x00000003, 0x00000003, 0x0004003b, 0x0000000b,
  0x0000000c, 0x00000002, 0x00050036, 0x00000004, 0x00000001, 0x00000000,
  0x00000005, 0x000200f8, 0x00000010, 0x0004003d, 0x00000007, 0x00000011,
  0x00000002, 0x00050041, 0x0000000f, 0x00000012, 0x0000000c, 0x0000000e,
  0x0004003d, 0x00000006, 0x00000013, 0x00000012, 0x0005008e, 0x00000007,
  0x00000014, 0x00000011, 0x00000013, 0x0003003e, 0x00000003, 0x00000014,
  0x000100fd, 0x00010038,
};

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

void expect_contains(const char* backend, const std::string& source, const char* needle) {
  if (source.find(needle) == std::string::npos) {
    std::fprintf(stderr, "%s output lacks \"%s\":\n%s\n", backend, needle, source.c_str());
    ++failures;
  }
}

}  // namespace

int main() {
  const std::vector<std::uint32_t> words(std::begin(shader), std::end(shader));

  try {
    spirv_cross::Parser parser(words);
    parser.parse();
    spirv_cross::CompilerGLSL glsl(std::move(parser.get_parsed_ir()));
    CHECK(glsl.get_execution_model() == spv::ExecutionModelFragment);

    const spirv_cross::ShaderResources resources = glsl.get_shader_resources();
    CHECK(resources.stage_inputs.size() == 1);
    CHECK(resources.stage_outputs.size() == 1);
    CHECK(resources.uniform_buffers.size() == 1);
    if (resources.stage_inputs.size() == 1) {
      CHECK(resources.stage_inputs[0].name == "color_in");
      CHECK(glsl.has_decoration(resources.stage_inputs[0].id, spv::DecorationLocation) &&
            glsl.get_decoration(resources.stage_inputs[0].id, spv::DecorationLocation) == 0);
    }
    if (resources.uniform_buffers.size() == 1) {
      const spirv_cross::Resource& block = resources.uniform_buffers[0];
      CHECK(block.name == "Params");
      CHECK(glsl.get_decoration(block.id, spv::DecorationDescriptorSet) == 2);
      CHECK(glsl.get_decoration(block.id, spv::DecorationBinding) == 3);
    }

    spirv_cross_util::rename_interface_variable(glsl, resources.stage_inputs, 0, "tint");
    spirv_cross::CompilerGLSL::Options options;
    options.es = true;
    options.version = 310;
    options.vulkan_semantics = true;
    glsl.set_common_options(options);
    const std::string glsl_source = glsl.compile();
    expect_contains("GLSL", glsl_source, "#version 310 es");
    expect_contains("GLSL", glsl_source, "set = 2, binding = 3");
    expect_contains("GLSL", glsl_source, "vec4 tint;");
    CHECK(glsl_source.find("color_in") == std::string::npos);

    spirv_cross::CompilerHLSL hlsl(words);
    spirv_cross::CompilerHLSL::Options hlsl_options;
    hlsl_options.shader_model = 50;
    hlsl.set_hlsl_options(hlsl_options);
    const std::string hlsl_source = hlsl.compile();
    expect_contains("HLSL", hlsl_source, "cbuffer Params");
    expect_contains("HLSL", hlsl_source, "register(b3");
    expect_contains("HLSL", hlsl_source, "SV_Target0");

    spirv_cross::CompilerMSL msl(words);
    const std::string msl_source = msl.compile();
    expect_contains("MSL", msl_source, "fragment main0_out main0(");
    expect_contains("MSL", msl_source, "[[color(0)]]");

    spirv_cross::CompilerCPP cpp(words);
    const std::string cpp_source = cpp.compile();
    expect_contains("C++", cpp_source, "#include \"spirv_cross/internal_interface.hpp\"");
    expect_contains("C++", cpp_source, "FragmentShader<Impl::Shader, Impl::Shader::Resources>");

    spirv_cross::CompilerReflection reflection(words);
    const std::string json = reflection.compile();
    expect_contains("JSON", json, "\"mode\" : \"frag\"");
    expect_contains("JSON", json, "\"ubos\"");
    expect_contains("JSON", json, "\"set\" : 2");
    expect_contains("JSON", json, "\"binding\" : 3");
  } catch (const spirv_cross::CompilerError& error) {
    std::fprintf(stderr, "unexpected CompilerError: %s\n", error.what());
    ++failures;
  }

  std::vector<std::uint32_t> bad = words;
  bad[0] = 0xdeadbeef;
  bool threw = false;
  try {
    spirv_cross::Parser parser(std::move(bad));
    parser.parse();
  } catch (const spirv_cross::CompilerError& error) {
    threw = std::string(error.what()).find("Invalid SPIRV format") != std::string::npos;
  }
  CHECK(threw);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("spirv-cross C++ consumer test passed");
  return 0;
}
