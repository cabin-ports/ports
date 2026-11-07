/* C consumer: the C interface preprocesses, compiles and links a GLSL
 * compute shader to SPIR-V 1.5, whose header, entry point and workgroup size
 * are checked, and reports a malformed shader's error. */
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char* what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

static const char compute_source[] =
    "#version 450\n"
    "#define WIDTH 8\n"
    "layout(local_size_x = WIDTH, local_size_y = 4) in;\n"
    "layout(std430, binding = 0) buffer Data { float values[]; } data;\n"
    "void main() {\n"
    "  uint i = gl_GlobalInvocationID.x;\n"
    "  data.values[i] = data.values[i] * 2.0;\n"
    "}\n";

static glslang_input_t make_input(const char* code) {
  glslang_input_t input;
  memset(&input, 0, sizeof input);
  input.language = GLSLANG_SOURCE_GLSL;
  input.stage = GLSLANG_STAGE_COMPUTE;
  input.client = GLSLANG_CLIENT_VULKAN;
  input.client_version = GLSLANG_TARGET_VULKAN_1_2;
  input.target_language = GLSLANG_TARGET_SPV;
  input.target_language_version = GLSLANG_TARGET_SPV_1_5;
  input.code = code;
  input.default_version = 100;
  input.default_profile = GLSLANG_NO_PROFILE;
  input.messages = GLSLANG_MSG_DEFAULT_BIT;
  input.resource = glslang_default_resource();
  return input;
}

int main(void) {
  glslang_version_t version;
  glslang_get_version(&version);
  CHECK(version.major == 16 && version.minor == 4 && version.patch == 0);
  CHECK(strcmp(version.flavor, "") == 0);

  CHECK(glslang_initialize_process() == 1);

  glslang_input_t input = make_input(compute_source);
  glslang_shader_t* shader = glslang_shader_create(&input);
  CHECK(glslang_shader_preprocess(shader, &input) == 1);
  const char* preprocessed = glslang_shader_get_preprocessed_code(shader);
  CHECK(strstr(preprocessed, "WIDTH") == NULL);
  CHECK(strstr(preprocessed, "local_size_x = 8") != NULL);
  CHECK(glslang_shader_parse(shader, &input) == 1);

  glslang_program_t* program = glslang_program_create();
  glslang_program_add_shader(program, shader);
  CHECK(glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT) == 1);
  glslang_program_SPIRV_generate(program, GLSLANG_STAGE_COMPUTE);

  /* The header: magic number, version 1.5, and glslang's generator word
   * (Khronos tool 8, generator version 11). */
  const size_t size = glslang_program_SPIRV_get_size(program);
  const unsigned int* words = glslang_program_SPIRV_get_ptr(program);
  CHECK(size > 5);
  CHECK(words[0] == 0x07230203u);
  CHECK(words[1] == 0x00010500u);
  CHECK(words[2] == ((8u << 16) | 11u));

  /* Every instruction's word count steps exactly to the end; the module
   * declares a GLCompute entry point "main" and LocalSize 8 4 1. */
  int entry_points = 0;
  int local_size_ok = 0;
  size_t at = 5;
  while (at < size) {
    const unsigned int count = words[at] >> 16;
    const unsigned int opcode = words[at] & 0xffffu;
    if (count == 0 || at + count > size) {
      break;
    }
    if (opcode == 15 /* OpEntryPoint */ && words[at + 1] == 5 /* GLCompute */) {
      ++entry_points;
      /* The name "main", its bytes packed into a word first byte lowest,
       * then a word of NUL padding. */
      CHECK(count >= 5 && words[at + 3] == 0x6e69616du && words[at + 4] == 0u);
    }
    if (opcode == 16 /* OpExecutionMode */ && words[at + 2] == 17 /* LocalSize */) {
      local_size_ok = words[at + 3] == 8 && words[at + 4] == 4 && words[at + 5] == 1;
    }
    at += count;
  }
  CHECK(at == size);
  CHECK(entry_points == 1);
  CHECK(local_size_ok);
  glslang_program_delete(program);
  glslang_shader_delete(shader);

  input = make_input("#version 450\nvoid main() { undeclared_name = 1; }\n");
  shader = glslang_shader_create(&input);
  CHECK(glslang_shader_preprocess(shader, &input) == 1);
  CHECK(glslang_shader_parse(shader, &input) == 0);
  const char* log = glslang_shader_get_info_log(shader);
  CHECK(strstr(log, "ERROR:") != NULL);
  CHECK(strstr(log, "'undeclared_name' : undeclared identifier") != NULL);
  glslang_shader_delete(shader);

  glslang_finalize_process();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("glslang C consumer test passed");
  return 0;
}
