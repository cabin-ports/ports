// No GL context is available, so the binding resolves through a fake
// resolver and glbinding-aux is checked with its context-free lookups.
#include <glbinding/AbstractFunction.h>
#include <glbinding/Binding.h>
#include <glbinding/FunctionCall.h>
#include <glbinding/Version.h>
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding-version.h>
#include <glbinding/glbinding.h>

#include <glbinding-aux/Meta.h>
#include <glbinding-aux/ValidVersions.h>
#include <glbinding-aux/types_to_string.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>
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

gl::GLfloat cleared[4];
int clearColorCalls = 0;

void GL_APIENTRY fakeClearColor(gl::GLfloat r, gl::GLfloat g, gl::GLfloat b,
                                gl::GLfloat a) {
  cleared[0] = r;
  cleared[1] = g;
  cleared[2] = b;
  cleared[3] = a;
  ++clearColorCalls;
}

}  // namespace

int main() {
  using glbinding::Binding;
  using glbinding::CallbackMask;
  using glbinding::Version;
  using glbinding::aux::Meta;
  using glbinding::aux::ValidVersions;

  // Resolution: every built-in function asks the resolver once.
  std::size_t lookups = 0;
  glbinding::initialize([&lookups](const char* name) -> glbinding::ProcAddress {
    ++lookups;
    return std::strcmp(name, "glClearColor") == 0
               ? reinterpret_cast<glbinding::ProcAddress>(&fakeClearColor)
               : nullptr;
  });
  check(Binding::size() == 3005u, "Binding::size");
  check(lookups == Binding::size(), "resolver lookups");
  check(Binding::ClearColor.isResolved(), "glClearColor resolved");
  check(!Binding::Clear.isResolved(), "glClear unresolved");
  check(std::string(Binding::ClearColor.name()) == "glClearColor",
        "function name");

  // Calls and callbacks.
  std::vector<std::string> afterCalls;
  std::string afterParameters;
  std::vector<std::string> unresolvedCalls;
  glbinding::setCallbackMask(CallbackMask::After | CallbackMask::Parameters |
                             CallbackMask::Unresolved);
  glbinding::setAfterCallback([&](const glbinding::FunctionCall& call) {
    afterCalls.push_back(call.function->name());
    std::ostringstream out;
    for (std::size_t i = 0; i < call.parameters.size(); ++i) {
      out << (i == 0 ? "" : ", ") << call.parameters[i].get();
    }
    afterParameters = out.str();
  });
  glbinding::setUnresolvedCallback(
      [&](const glbinding::AbstractFunction& function) {
        unresolvedCalls.push_back(function.name());
      });

  gl::glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
  check(clearColorCalls == 1 && cleared[0] == 0.25f && cleared[1] == 0.5f &&
            cleared[2] == 0.75f && cleared[3] == 1.0f,
        "glClearColor reached the resolved function");
  check(afterCalls == std::vector<std::string>{"glClearColor"},
        "after callback");
  check(afterParameters == "0.25, 0.5, 0.75, 1", "after callback parameters");

  gl::glClear(gl::GL_COLOR_BUFFER_BIT);
  check(unresolvedCalls == std::vector<std::string>{"glClear"},
        "unresolved callback");
  check(afterCalls.size() == 1u && clearColorCalls == 1,
        "unresolved call skipped");

  // directCall goes through Function<>::Signature, instantiated here in the
  // consumer, and skips the callbacks.
  Binding::ClearColor.directCall(0.125f, 0.25f, 0.375f, 0.5f);
  check(clearColorCalls == 2 && cleared[0] == 0.125f && cleared[1] == 0.25f &&
            cleared[2] == 0.375f && cleared[3] == 0.5f,
        "directCall reached the resolved function");
  check(afterCalls.size() == 1u, "directCall skipped the callbacks");

  // Contexts keep their own function pointers.
  glbinding::initialize(
      glbinding::ContextHandle{1},
      [](const char*) -> glbinding::ProcAddress { return nullptr; }, true,
      true);
  check(!Binding::ClearColor.isResolved(), "second context unresolved");
  glbinding::useContext(glbinding::ContextHandle{0});
  check(Binding::ClearColor.isResolved(), "first context restored");

  // Enums: several names share 0x0004, and getString picks the shortest.
  check(static_cast<unsigned int>(gl::GL_TRIANGLES) == 0x0004u,
        "GL_TRIANGLES value");
  check(Meta::getString(gl::GL_TRIANGLES) == "GL_TRIANGLES", "enum string");
  check(Meta::getStrings(gl::GL_TRIANGLES).size() == 4u, "enum strings");
  check(Meta::getEnum("GL_TRIANGLES") == gl::GL_TRIANGLES, "enum lookup");
  check(Meta::getEnum("GL_LINE_TO_NV") == gl::GL_TRIANGLES,
        "enum alias lookup");
  check(static_cast<unsigned int>(Meta::getEnum("GL_NOT_AN_ENUM")) ==
            0xFFFFFFFFu,
        "unknown enum");
  std::ostringstream enumText;
  enumText << gl::GL_TRIANGLES;
  check(enumText.str() ==
            "GL_DRAW_ELEMENTS_STRIP_COMMAND_NV | GL_LINE_TO_NV | "
            "GL_SUBGROUP_FEATURE_ARITHMETIC_BIT_KHR | GL_TRIANGLES",
        "enum stream output");

  // Booleans and bitfields.
  check(Meta::getString(gl::GL_TRUE) == "GL_TRUE", "boolean string");
  check(Meta::getBoolean("GL_FALSE") == gl::GL_FALSE, "boolean lookup");
  check(Meta::getString(gl::ClearBufferMask::GL_COLOR_BUFFER_BIT) ==
            "GL_COLOR_BUFFER_BIT",
        "bitfield string");
  check(static_cast<unsigned int>(Meta::getBitfield("GL_DEPTH_BUFFER_BIT")) ==
            0x0100u,
        "bitfield lookup");
  std::ostringstream maskText;
  maskText << (gl::ClearBufferMask::GL_COLOR_BUFFER_BIT |
               gl::ClearBufferMask::GL_DEPTH_BUFFER_BIT);
  check(maskText.str() == "GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT",
        "bitfield stream output");

  // Extensions.
  const auto computeShader = gl::GLextension::GL_ARB_compute_shader;
  check(Meta::getExtension("GL_ARB_compute_shader") == computeShader,
        "extension lookup");
  check(Meta::getString(computeShader) == "GL_ARB_compute_shader",
        "extension string");
  check(Meta::version(computeShader) == Version(4, 3), "extension version");
  check(Meta::extensions("glDispatchCompute") ==
            std::set<gl::GLextension>{computeShader},
        "extensions of a function");
  std::set<std::string> computeFunctions;
  for (const auto* function : Meta::functions(computeShader)) {
    computeFunctions.insert(function->name());
  }
  check(computeFunctions ==
            std::set<std::string>{"glDispatchCompute",
                                  "glDispatchComputeIndirect"},
        "functions of an extension");
  check(Meta::glRevision() == 20250414, "gl.xml revision");

  // Versions.
  check(std::string(GLBINDING_VERSION) == "3.5.0", "glbinding version");
  check(Version(4, 6).toString() == "4.6", "version string");
  check(Version().toString() == "-.-", "null version string");
  check(Version(3, 3) < Version(4, 0) && Version(4, 6) > Version(4, 5),
        "version order");
  check(ValidVersions::latest() == Version(4, 6), "latest version");
  check(ValidVersions::isValid(Version(3, 2)) &&
            !ValidVersions::isValid(Version(3, 4)),
        "valid versions");
  check(ValidVersions::nearest(Version(3, 4)) == Version(4, 0),
        "nearest version");
  std::ostringstream versionText;
  versionText << Version(4, 6);
  check(versionText.str() == "4.6", "version stream output");

  if (failures != 0) {
    return 1;
  }
  std::puts("glbinding ok");
  return 0;
}
