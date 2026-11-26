#include <PerlinNoise.hpp>

#include <array>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>
#include <utility>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  CHECK(SIVPERLIN_VERSION == 30000);

  // The seeded permutation is std::mt19937 plus upstream's own shuffle, so it
  // is defined by the standard and identical on every platform.
  std::array<std::uint8_t, 256> expected;
  std::iota(expected.begin(), expected.end(), std::uint8_t{0});
  std::mt19937 rng{123456u};
  for (std::size_t i = 1; i < expected.size(); ++i) {
    std::swap(expected[i], expected[rng() % (i + 1)]);
  }

  const siv::PerlinNoise seeded{123456u};
  CHECK(seeded.serialize() == expected);
  const siv::PerlinNoise from_urbg{std::mt19937{123456u}};
  CHECK(from_urbg.serialize() == expected);

  // The default permutation is Ken Perlin's reference table.
  const siv::PerlinNoise reference;
  CHECK(reference.serialize()[0] == 151 && reference.serialize()[255] == 180);

  // Noise is exactly 0 at integer lattice points, for any permutation.
  for (int i = -3; i <= 3; ++i) {
    for (int j = -3; j <= 3; ++j) {
      CHECK(seeded.noise3D(i, j, 2 * i - j) == 0.0);
      CHECK(reference.noise3D(i, j, 2 * i - j) == 0.0);
    }
  }
  CHECK(seeded.noise3D_01(4.0, -5.0, 6.0) == 0.5);

  // Same seed gives same noise, another seed differs at a non-lattice point.
  const siv::PerlinNoise same{123456u};
  const siv::PerlinNoise other{654321u};
  CHECK(same.noise3D(1.3, 2.7, 0.9) == seeded.noise3D(1.3, 2.7, 0.9));
  CHECK(other.noise3D(1.3, 2.7, 0.9) != seeded.noise3D(1.3, 2.7, 0.9));
  CHECK(other.noise3D(1.3, 2.7, 0.9) != reference.noise3D(1.3, 2.7, 0.9));

  // serialize/deserialize/reseed reproduce the state.
  siv::PerlinNoise copy;
  copy.deserialize(seeded.serialize());
  CHECK(copy.noise2D(5.5, 7.25) == seeded.noise2D(5.5, 7.25));
  copy.reseed(654321u);
  CHECK(copy.noise2D(5.5, 7.25) == other.noise2D(5.5, 7.25));

  // The lower dimensions fix the remaining coordinates at upstream's defaults.
  // Compared with a tolerance: inlining may contract floating-point ops.
  CHECK(std::fabs(seeded.noise2D(1.3, 2.7) -
                  seeded.noise3D(1.3, 2.7, SIVPERLIN_DEFAULT_Z)) < 1e-12);
  CHECK(std::fabs(seeded.noise1D(1.3) - seeded.noise3D(1.3, SIVPERLIN_DEFAULT_Y,
                                                       SIVPERLIN_DEFAULT_Z)) <
        1e-12);

  // Ranges over a grid.
  bool varies = false;
  for (double x = -4.0; x < 6.0; x += 0.37) {
    for (double y = -4.0; y < 6.0; y += 0.41) {
      const double n = seeded.noise2D(x, y);
      CHECK(n >= -1.0 && n <= 1.0);
      const double n01 = seeded.noise2D_01(x, y);
      CHECK(n01 >= 0.0 && n01 <= 1.0 && n01 == n * 0.5 + 0.5);
      CHECK(seeded.octave2D_01(x, y, 4) >= 0.0 &&
            seeded.octave2D_01(x, y, 4) <= 1.0);
      CHECK(seeded.octave2D_11(x, y, 4) >= -1.0 &&
            seeded.octave2D_11(x, y, 4) <= 1.0);
      CHECK(seeded.normalizedOctave2D(x, y, 4, 0.7) >= -1.0 &&
            seeded.normalizedOctave2D(x, y, 4, 0.7) <= 1.0);
      varies = varies || n != 0.0;
    }
  }
  CHECK(varies);

  // Octaves: one octave is the noise; each further one doubles the frequency
  // and scales the amplitude by the persistence; normalizing divides by the
  // amplitude sum.
  const double x = 1.3, y = 2.7, z = 0.9;
  CHECK(seeded.octave1D(x, 1) == seeded.noise1D(x));
  CHECK(seeded.octave2D(x, y, 1) == seeded.noise2D(x, y));
  CHECK(seeded.octave3D(x, y, z, 1) == seeded.noise3D(x, y, z));
  CHECK(seeded.octave3D(x, y, z, 0) == 0.0);
  CHECK(seeded.octave1D(x, 2, 0.25) ==
        seeded.noise1D(x) + seeded.noise1D(2 * x) * 0.25);
  CHECK(seeded.octave2D(x, y, 3) ==
        seeded.noise2D(x, y) + seeded.noise2D(2 * x, 2 * y) * 0.5 +
            seeded.noise2D(4 * x, 4 * y) * 0.25);
  CHECK(seeded.octave3D(x, y, z, 2, 0.5) ==
        seeded.noise3D(x, y, z) + seeded.noise3D(2 * x, 2 * y, 2 * z) * 0.5);
  CHECK(seeded.normalizedOctave3D(x, y, z, 2, 0.5) ==
        seeded.octave3D(x, y, z, 2, 0.5) / 1.5);

  return failures == 0 ? 0 : 1;
}
