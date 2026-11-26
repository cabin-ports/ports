// The _DONT_USE_SHADER_EXCEPTION mode reports errors to stderr and in the
// object's state instead of throwing.  GL is faked as in ../../main.cc: a
// shader whose source contains "ERROR" fails to compile; a program with no
// shader attached fails to link.
#define _DONT_USE_SHADER_EXCEPTION
#include <cstdio>
#include <cstring>
#include <string>

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

namespace {
std::string lastSource;
int attached = 0;
const char* const log = "bad";
GLuint createShader(GLenum) { return 1; }
void shaderSource(GLuint, GLsizei, const GLchar* const* s, const GLint*) { lastSource = s[0]; }
void noop1(GLuint) {}
void getShaderiv(GLuint, GLenum p, GLint* out) {
  *out = p == GL_SHADER_TYPE            ? static_cast<GLint>(GL_VERTEX_SHADER)
         : p == GL_COMPILE_STATUS       ? lastSource.find("ERROR") == std::string::npos
                                        : static_cast<GLint>(std::strlen(log)) + 1;
}
void infoLog(GLuint, GLsizei n, GLsizei*, GLchar* buf) { std::memcpy(buf, log, n - 1); }
GLuint createProgram() { return 2; }
void attach(GLuint, GLuint) { ++attached; }
void getProgramiv(GLuint, GLenum p, GLint* out) {
  *out = p == GL_LINK_STATUS ? attached > 0 : static_cast<GLint>(std::strlen(log)) + 1;
}
}  // namespace

GLuint (*glCreateShader)(GLenum) = createShader;
void (*glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = shaderSource;
void (*glCompileShader)(GLuint) = noop1;
void (*glGetShaderiv)(GLuint, GLenum, GLint*) = getShaderiv;
void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = infoLog;
void (*glDeleteShader)(GLuint) = noop1;
GLuint (*glCreateProgram)() = createProgram;
void (*glAttachShader)(GLuint, GLuint) = attach;
void (*glLinkProgram)(GLuint) = noop1;
void (*glGetProgramiv)(GLuint, GLenum, GLint*) = getProgramiv;
void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = infoLog;
void (*glUseProgram)(GLuint) = noop1;
void (*glDeleteProgram)(GLuint) = noop1;

#include <GLShaderPP/ShaderProgram.h>

int main() {
  using GLShaderPP::CShader;
  using GLShaderPP::CShaderProgram;
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::fprintf(stderr, "check failed: %s\n", what);
      ++failures;
    }
  };

  CShader bad{GL_VERTEX_SHADER, std::string("ERROR")};
  check(bad.GetCompileState() == CShader::ShaderCompileState::compileError, "compile error state");

  CShaderProgram program;
  program << bad;
  check(program.GetLinkingStatus() == CShaderProgram::LinkingStatus::prepareLinkError, "prepare-link error state");

  CShaderProgram empty;
  empty.Link();
  check(empty.GetLinkingStatus() == CShaderProgram::LinkingStatus::linkingError, "link error state");

  CShader good{GL_VERTEX_SHADER, std::string("void main(){}")};
  CShaderProgram ok{good};
  check(ok.GetLinkingStatus() == CShaderProgram::LinkingStatus::linkingOk, "link ok state");

  if (failures == 0) std::puts("ok");
  return failures != 0;
}
