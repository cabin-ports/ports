// There is no OpenGL context here, so the test supplies the GL names the headers
// use (Khronos values) before including them, backed by a small fake.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLchar = char;
constexpr GLint GL_TRUE = 1;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
constexpr GLenum GL_SHADER_TYPE = 0x8B4F;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_GEOMETRY_SHADER = 0x8DD9;
constexpr GLenum GL_TESS_CONTROL_SHADER = 0x8E88;
constexpr GLenum GL_TESS_EVALUATION_SHADER = 0x8E87;
constexpr GLenum GL_COMPUTE_SHADER = 0x91B9;

GLuint (*glCreateShader)(GLenum) = nullptr;
void (*glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
void (*glCompileShader)(GLuint) = nullptr;
void (*glGetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
void (*glDeleteShader)(GLuint) = nullptr;
GLuint (*glCreateProgram)() = nullptr;
void (*glAttachShader)(GLuint, GLuint) = nullptr;
void (*glLinkProgram)(GLuint) = nullptr;
void (*glGetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
void (*glUseProgram)(GLuint) = nullptr;
void (*glDeleteProgram)(GLuint) = nullptr;

#include <GLShaderPP/ShaderProgram.h>

namespace {

using GLShaderPP::CShader;
using GLShaderPP::CShaderException;
using GLShaderPP::CShaderProgram;
using Type = CShaderException::ExceptionType;

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "line %d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(x) check((x), #x, __LINE__)

// Fake driver: a shader fails to compile when its source contains "ERROR";
// a program fails to link when it has no shader attached.
struct FakeShader {
  GLenum type;
  std::string source;
};
std::map<GLuint, FakeShader> shaders;
std::map<GLuint, std::vector<GLuint>> programs;
std::vector<GLuint> deletedShaders, deletedPrograms;
GLuint nextId = 1, usedProgram = 0;

const char* const compileLog = "0:1: bad token";
const char* const linkLog = "no shader attached";

GLuint fakeCreateShader(GLenum t) {
  shaders[nextId] = {t, ""};
  return nextId++;
}
void fakeShaderSource(GLuint s, GLsizei n, const GLchar* const* str, const GLint*) {
  CHECK(n == 1);
  shaders[s].source = str[0];
}
void fakeCompileShader(GLuint) {}
void fakeGetShaderiv(GLuint s, GLenum p, GLint* out) {
  const FakeShader& sh = shaders[s];
  if (p == GL_SHADER_TYPE)
    *out = static_cast<GLint>(sh.type);
  else if (p == GL_COMPILE_STATUS)
    *out = sh.source.find("ERROR") == std::string::npos;
  else if (p == GL_INFO_LOG_LENGTH)
    *out = static_cast<GLint>(std::strlen(compileLog)) + 1;
}
void fakeGetShaderInfoLog(GLuint, GLsizei n, GLsizei*, GLchar* buf) { std::memcpy(buf, compileLog, n - 1); }
void fakeDeleteShader(GLuint s) { deletedShaders.push_back(s); }
GLuint fakeCreateProgram() {
  programs[nextId];
  return nextId++;
}
void fakeAttachShader(GLuint p, GLuint s) { programs[p].push_back(s); }
void fakeLinkProgram(GLuint) {}
void fakeGetProgramiv(GLuint p, GLenum name, GLint* out) {
  if (name == GL_LINK_STATUS)
    *out = !programs[p].empty();
  else if (name == GL_INFO_LOG_LENGTH)
    *out = static_cast<GLint>(std::strlen(linkLog)) + 1;
}
void fakeGetProgramInfoLog(GLuint, GLsizei n, GLsizei*, GLchar* buf) { std::memcpy(buf, linkLog, n - 1); }
void fakeUseProgram(GLuint p) { usedProgram = p; }
void fakeDeleteProgram(GLuint p) { deletedPrograms.push_back(p); }

void installFakes() {
  glCreateShader = fakeCreateShader;
  glShaderSource = fakeShaderSource;
  glCompileShader = fakeCompileShader;
  glGetShaderiv = fakeGetShaderiv;
  glGetShaderInfoLog = fakeGetShaderInfoLog;
  glDeleteShader = fakeDeleteShader;
  glCreateProgram = fakeCreateProgram;
  glAttachShader = fakeAttachShader;
  glLinkProgram = fakeLinkProgram;
  glGetProgramiv = fakeGetProgramiv;
  glGetProgramInfoLog = fakeGetProgramInfoLog;
  glUseProgram = fakeUseProgram;
  glDeleteProgram = fakeDeleteProgram;
}

template <class F>
bool throwsType(F f, Type t, const std::string& what) {
  try {
    f();
  } catch (const CShaderException& e) {
    return e.type() == t && what == e.what();
  }
  return false;
}

}  // namespace

int main() {
  const std::string noContext = "Error: OpenGL context seems not to be properly initialised.";

  // 1. Without a loaded GL, construction throws GlewInit.
  CHECK(throwsType([] { CShader s{GL_VERTEX_SHADER}; }, Type::GlewInit, noContext));
  CHECK(throwsType([] { CShaderProgram p; }, Type::GlewInit, noContext));

  installFakes();

  // 2. Compiling from a string and from a stream; the shader stage names.
  {
    CShader vs{GL_VERTEX_SHADER, std::string("void main(){}")};
    CHECK(vs.GetCompileState() == CShader::ShaderCompileState::compileOk);
    CHECK(vs.GetType() == "vertex");
    CHECK(shaders[vs.GetShaderId()].source == "void main(){}");

    std::istringstream in("void main(){ /* frag */ }");
    CShader fs{GL_FRAGMENT_SHADER, in};
    CHECK(fs.GetCompileState() == CShader::ShaderCompileState::compileOk);
    CHECK(shaders[fs.GetShaderId()].source == "void main(){ /* frag */ }");

    CHECK(CShader{GL_FRAGMENT_SHADER}.GetType() == "fragment");
    CHECK(CShader{GL_GEOMETRY_SHADER}.GetType() == "geometry");
    CHECK(CShader{GL_TESS_CONTROL_SHADER}.GetType() == "tesselation control");
    CHECK(CShader{GL_TESS_EVALUATION_SHADER}.GetType() == "tesselation evaluation");
    CHECK(CShader{GL_COMPUTE_SHADER}.GetType() == "compute");
    CHECK(CShader{0}.GetType() == "unknown");

    // An empty shader compiles only after SetSource and Compile.
    CShader late{GL_VERTEX_SHADER};
    CHECK(late.GetCompileState() == CShader::ShaderCompileState::notCompiled);
    late.SetSource("void main(){}");
    late.Compile();
    CHECK(late.GetCompileState() == CShader::ShaderCompileState::compileOk);
  }

  // 3. Compile errors and unreadable streams throw typed exceptions.
  CHECK(throwsType([] { CShader s{GL_VERTEX_SHADER, std::string("ERROR")}; }, Type::CompilationError,
                   std::string("An error occured during vertex shader compilation\n") + compileLog));
  CHECK(throwsType(
      [] {
        std::istringstream bad;
        bad.setstate(std::ios::badbit);
        CShader s{GL_GEOMETRY_SHADER, bad};
      },
      Type::BadSourceStream, "Can not open geometry shader sources"));

  // 4. Programs: attach, link, use, delete.
  deletedShaders.clear();
  GLuint programId = 0, vsId = 0, fsId = 0;
  {
    CShader vs{GL_VERTEX_SHADER, std::string("void main(){}")};
    CShader fs{GL_FRAGMENT_SHADER, std::string("void main(){}")};
    vsId = vs.GetShaderId();
    fsId = fs.GetShaderId();
    CShaderProgram program{vs, fs};
    programId = program.GetProgramId();
    CHECK(program.GetLinkingStatus() == CShaderProgram::LinkingStatus::linkingOk);
    CHECK((programs[programId] == std::vector<GLuint>{vsId, fsId}));
    program.Use();
    CHECK(usedProgram == programId);

    CShaderProgram manual;
    manual << vs << fs;
    CHECK(manual.GetLinkingStatus() == CShaderProgram::LinkingStatus::notLinked);
    manual.Link();
    CHECK(manual.GetLinkingStatus() == CShaderProgram::LinkingStatus::linkingOk);
  }
  CHECK(deletedPrograms.size() == 2 && deletedPrograms[1] == programId);
  CHECK(deletedShaders.size() == 2 && deletedShaders[0] == fsId && deletedShaders[1] == vsId);

  // 5. Attaching an uncompiled shader and linking nothing throw typed exceptions.
  CHECK(throwsType(
      [] {
        CShader raw{GL_VERTEX_SHADER};
        CShaderProgram p;
        p << raw;
      },
      Type::PrepareLinkError, "vertex shader has not been compiled before being attached to program"));
  CHECK(throwsType([] { CShaderProgram p; p.Link(); }, Type::LinkError,
                   std::string("An error occured during program linking\n") + linkLog));

  // 6. The shader-pack constructor also requires a loaded GL.
  {
    CShader vs{GL_VERTEX_SHADER, std::string("void main(){}")};
    glCreateProgram = nullptr;
    CHECK(throwsType([&] { CShaderProgram p{vs}; }, Type::GlewInit, noContext));
  }

  if (failures == 0) std::puts("ok");
  return failures != 0;
}
