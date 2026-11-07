// Kept C++98-clean: the probe compiles it at the lowest standard
// satisfying interface-cxx-standard (C++98, or MSVC's oldest, C++14).
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>

static int fail(const char *what) {
  std::fprintf(stderr, "glm: %s\n", what);
  return 1;
}

static bool near(const glm::vec4 &a, const glm::vec4 &b) {
  return glm::all(glm::epsilonEqual(a, b, 1e-5f));
}

static bool near3(const glm::vec3 &a, const glm::vec3 &b) {
  return glm::all(glm::epsilonEqual(a, b, 1e-5f));
}

int main() {
  const glm::vec3 x(1.0f, 0.0f, 0.0f), y(0.0f, 1.0f, 0.0f);
  if (glm::cross(x, y) != glm::vec3(0.0f, 0.0f, 1.0f)) return fail("cross");
  if (glm::dot(glm::vec3(1, 2, 3), glm::vec3(4, 5, 6)) != 32.0f)
    return fail("dot");
  if (glm::length(glm::vec3(3, 4, 0)) != 5.0f) return fail("length");
  if (!near3(glm::normalize(glm::vec3(3, 4, 0)), glm::vec3(0.6f, 0.8f, 0.0f)))
    return fail("normalize");

  const glm::vec4 point(1.0f, 1.0f, 1.0f, 1.0f);
  const glm::mat4 identity(1.0f);
  if (glm::translate(identity, glm::vec3(1, 2, 3)) * point !=
      glm::vec4(2, 3, 4, 1))
    return fail("translate");
  if (!near(glm::rotate(identity, glm::half_pi<float>(),
                        glm::vec3(0, 0, 1)) *
                glm::vec4(1, 0, 0, 1),
            glm::vec4(0, 1, 0, 1)))
    return fail("rotate");
  if (glm::scale(identity, glm::vec3(2, 3, 4)) * point != glm::vec4(2, 3, 4, 1))
    return fail("scale");

  // OpenGL clip space (right-handed, depth -1..1) is GLM's default.
  const glm::mat4 proj =
      glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
  if (glm::abs(proj[0][0] - 1.0f) > 1e-5f || glm::abs(proj[1][1] - 1.0f) > 1e-5f ||
      glm::abs(proj[2][2] + 101.0f / 99.0f) > 1e-5f || proj[2][3] != -1.0f ||
      glm::abs(proj[3][2] + 200.0f / 99.0f) > 1e-5f || proj[3][3] != 0.0f)
    return fail("perspective");
  const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0.0f), y);
  if (!near(view * glm::vec4(0, 0, 0, 1), glm::vec4(0, 0, -5, 1)))
    return fail("lookAt");
  const glm::mat4 m = view * glm::rotate(identity, 0.5f, glm::vec3(1, 1, 0));
  const glm::mat4 round_trip = glm::inverse(m) * m;
  for (int c = 0; c < 4; ++c)
    if (!near(round_trip[c], identity[c])) return fail("inverse");

  const glm::quat q = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
  if (!near3(q * x, y)) return fail("quaternion rotation");
  const float h = glm::sqrt(0.5f);
  if (!near3(glm::slerp(glm::quat(1, 0, 0, 0), q, 0.5f) * x,
             glm::vec3(h, h, 0.0f)))
    return fail("slerp");
  if (!near(glm::mat4_cast(q) * glm::vec4(1, 0, 0, 1), glm::vec4(0, 1, 0, 1)))
    return fail("mat4_cast");

  // x lands in the low byte (little-endian hosts); 0.5 and 0.25 round
  // to 128 and 64.
  if (glm::packUnorm4x8(glm::vec4(0.0f, 1.0f, 0.5f, 0.25f)) != 0x4080FF00u)
    return fail("packUnorm4x8");
  if (glm::unpackUnorm4x8(0xFF000080u) !=
      glm::vec4(128.0f / 255.0f, 0.0f, 0.0f, 1.0f))
    return fail("unpackUnorm4x8");
  if (glm::value_ptr(proj)[11] != -1.0f) return fail("value_ptr (column-major)");
  return 0;
}
