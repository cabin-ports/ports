/* Through the C API, a compute shader hand-assembled below as SPIR-V words
   cross-compiles to each backend the package enables (GLSL, HLSL, MSL, C++
   and JSON reflection), its storage buffer reflects with its binding, and a
   module with a bad header fails to parse with an error, not an abort. */
#include <spirv_cross_c.h>

#include <stdio.h>
#include <string.h>

/* #version 450
   layout(local_size_x = 64) in;
   layout(binding = 1, std430) buffer Data { float values[]; } data;
   void main() { data.values[gl_GlobalInvocationID.x] *= 2.0; }
   as SPIR-V 1.0, the buffer a Uniform BufferBlock. */
static const SpvId shader[] = {
    0x07230203, 0x00010000, 0x00000000, 0x00000019, 0x00000000, 0x00020011,
    0x00000001, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000005,
    0x00000001, 0x6e69616d, 0x00000000, 0x00000002, 0x00060010, 0x00000001,
    0x00000011, 0x00000040, 0x00000001, 0x00000001, 0x00040005, 0x00000001,
    0x6e69616d, 0x00000000, 0x00040005, 0x0000000b, 0x61746144, 0x00000000,
    0x00050006, 0x0000000b, 0x00000000, 0x756c6176, 0x00007365, 0x00040005,
    0x0000000d, 0x61746164, 0x00000000, 0x00040047, 0x00000002, 0x0000000b,
    0x0000001c, 0x00040047, 0x0000000a, 0x00000006, 0x00000004, 0x00050048,
    0x0000000b, 0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x0000000b,
    0x00000003, 0x00040047, 0x0000000d, 0x00000022, 0x00000000, 0x00040047,
    0x0000000d, 0x00000021, 0x00000001, 0x00020013, 0x00000003, 0x00030021,
    0x00000004, 0x00000003, 0x00030016, 0x00000005, 0x00000020, 0x00040015,
    0x00000006, 0x00000020, 0x00000000, 0x00040015, 0x00000007, 0x00000020,
    0x00000001, 0x00040017, 0x00000008, 0x00000006, 0x00000003, 0x00040020,
    0x00000009, 0x00000001, 0x00000008, 0x0003001d, 0x0000000a, 0x00000005,
    0x0003001e, 0x0000000b, 0x0000000a, 0x00040020, 0x0000000c, 0x00000002,
    0x0000000b, 0x00040020, 0x00000010, 0x00000001, 0x00000006, 0x00040020,
    0x00000012, 0x00000002, 0x00000005, 0x0004002b, 0x00000007, 0x0000000e,
    0x00000000, 0x0004002b, 0x00000006, 0x00000011, 0x00000000, 0x0004002b,
    0x00000005, 0x0000000f, 0x40000000, 0x0004003b, 0x00000009, 0x00000002,
    0x00000001, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000002, 0x00050036,
    0x00000003, 0x00000001, 0x00000000, 0x00000004, 0x000200f8, 0x00000013,
    0x00050041, 0x00000010, 0x00000014, 0x00000002, 0x00000011, 0x0004003d,
    0x00000006, 0x00000015, 0x00000014, 0x00060041, 0x00000012, 0x00000016,
    0x0000000d, 0x0000000e, 0x00000015, 0x0004003d, 0x00000005, 0x00000017,
    0x00000016, 0x00050085, 0x00000005, 0x00000018, 0x00000017, 0x0000000f,
    0x0003003e, 0x00000016, 0x00000018, 0x000100fd, 0x00010038,
};

static int failures = 0;

static void check(int ok, int line, const char *what)
{
    if (!ok)
    {
        fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

static void expect_contains(const char *backend, const char *source, const char *needle)
{
    if (source == NULL || strstr(source, needle) == NULL)
    {
        fprintf(stderr, "%s output lacks \"%s\":\n%s\n", backend, needle, source != NULL ? source : "(none)");
        ++failures;
    }
}

/* Compiles the module with one backend, setting at most one option. */
static const char *compile(spvc_context context, spvc_parsed_ir ir, spvc_backend backend, const char *name,
                           spvc_compiler_option option, unsigned value)
{
    spvc_compiler compiler = NULL;
    spvc_compiler_options options = NULL;
    const char *source = NULL;
    if (spvc_context_create_compiler(context, backend, ir, SPVC_CAPTURE_MODE_COPY, &compiler) != SPVC_SUCCESS ||
        spvc_compiler_create_compiler_options(compiler, &options) != SPVC_SUCCESS ||
        (option != SPVC_COMPILER_OPTION_UNKNOWN &&
         spvc_compiler_options_set_uint(options, option, value) != SPVC_SUCCESS) ||
        spvc_compiler_install_compiler_options(compiler, options) != SPVC_SUCCESS ||
        spvc_compiler_compile(compiler, &source) != SPVC_SUCCESS)
    {
        fprintf(stderr, "%s backend failed: %s\n", name, spvc_context_get_last_error_string(context));
        ++failures;
        return NULL;
    }
    return source;
}

static void reflect(spvc_context context, spvc_parsed_ir ir)
{
    spvc_compiler compiler = NULL;
    spvc_resources resources = NULL;
    const spvc_reflected_resource *buffers = NULL;
    size_t count = 0;
    if (spvc_context_create_compiler(context, SPVC_BACKEND_NONE, ir, SPVC_CAPTURE_MODE_COPY, &compiler) != SPVC_SUCCESS ||
        spvc_compiler_create_shader_resources(compiler, &resources) != SPVC_SUCCESS ||
        spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, &buffers, &count) !=
            SPVC_SUCCESS)
    {
        fprintf(stderr, "reflection failed: %s\n", spvc_context_get_last_error_string(context));
        ++failures;
        return;
    }
    CHECK(count == 1);
    if (count == 1)
    {
        CHECK(strcmp(buffers[0].name, "Data") == 0);
        CHECK(spvc_compiler_has_decoration(compiler, buffers[0].id, SpvDecorationBinding) &&
              spvc_compiler_get_decoration(compiler, buffers[0].id, SpvDecorationBinding) == 1);
        CHECK(spvc_compiler_has_decoration(compiler, buffers[0].id, SpvDecorationDescriptorSet) &&
              spvc_compiler_get_decoration(compiler, buffers[0].id, SpvDecorationDescriptorSet) == 0);
    }
    CHECK(spvc_compiler_get_execution_model(compiler) == SpvExecutionModelGLCompute);
    CHECK(spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionModeLocalSize, 0) == 64);
}

int main(void)
{
    spvc_context context = NULL;
    spvc_parsed_ir ir = NULL;
    SpvId bad[sizeof(shader) / sizeof(shader[0])];
    unsigned major = 0, minor = 0, patch = 0;
    const char *source;

    spvc_get_version(&major, &minor, &patch);
    CHECK(major == SPVC_C_API_VERSION_MAJOR && minor == SPVC_C_API_VERSION_MINOR && patch == SPVC_C_API_VERSION_PATCH);

    if (spvc_context_create(&context) != SPVC_SUCCESS)
    {
        fputs("spvc_context_create failed\n", stderr);
        return 1;
    }
    if (spvc_context_parse_spirv(context, shader, sizeof(shader) / sizeof(shader[0]), &ir) != SPVC_SUCCESS)
    {
        fprintf(stderr, "parse failed: %s\n", spvc_context_get_last_error_string(context));
        spvc_context_destroy(context);
        return 1;
    }

    reflect(context, ir);

    source = compile(context, ir, SPVC_BACKEND_GLSL, "GLSL", SPVC_COMPILER_OPTION_GLSL_VERSION, 430);
    expect_contains("GLSL", source, "#version 430");
    expect_contains("GLSL", source, "local_size_x = 64");
    expect_contains("GLSL", source, "binding = 1, std430) buffer Data");
    expect_contains("GLSL", source, "float values[];");
    expect_contains("GLSL", source, "gl_GlobalInvocationID.x");

    source = compile(context, ir, SPVC_BACKEND_HLSL, "HLSL", SPVC_COMPILER_OPTION_HLSL_SHADER_MODEL, 50);
    expect_contains("HLSL", source, "RWByteAddressBuffer data : register(u1)");
    expect_contains("HLSL", source, "[numthreads(64, 1, 1)]");
    expect_contains("HLSL", source, "SV_DispatchThreadID");

    source = compile(context, ir, SPVC_BACKEND_MSL, "MSL", SPVC_COMPILER_OPTION_UNKNOWN, 0);
    expect_contains("MSL", source, "kernel void main0(");
    expect_contains("MSL", source, "thread_position_in_grid");

    source = compile(context, ir, SPVC_BACKEND_CPP, "C++", SPVC_COMPILER_OPTION_UNKNOWN, 0);
    expect_contains("C++", source, "#include \"spirv_cross/internal_interface.hpp\"");
    expect_contains("C++", source, "ComputeShader<Impl::Shader, Impl::Shader::Resources, 64, 1, 1>");

    source = compile(context, ir, SPVC_BACKEND_JSON, "JSON", SPVC_COMPILER_OPTION_UNKNOWN, 0);
    expect_contains("JSON", source, "\"mode\" : \"comp\"");
    expect_contains("JSON", source, "\"ssbos\"");
    expect_contains("JSON", source, "\"binding\" : 1");

    /* A wrong magic number: the C++ exception becomes an error code. */
    memcpy(bad, shader, sizeof(shader));
    bad[0] = 0xdeadbeef;
    ir = NULL;
    CHECK(spvc_context_parse_spirv(context, bad, sizeof(bad) / sizeof(bad[0]), &ir) == SPVC_ERROR_INVALID_SPIRV);
    CHECK(strstr(spvc_context_get_last_error_string(context), "Invalid SPIRV format") != NULL);

    spvc_context_destroy(context);
    if (failures != 0)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("spirv-cross C consumer test passed");
    return 0;
}
