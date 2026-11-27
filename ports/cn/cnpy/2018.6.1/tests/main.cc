#include <cnpy.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

std::string temp_path(const char *name) {
  const char *dir = std::getenv("TMPDIR");
  if (!dir) dir = std::getenv("TEMP");
  if (!dir) dir = "/tmp";
  return std::string(dir) + "/cnpy-consumer-" + name;
}

} // namespace

int main() {
  const std::string npy = temp_path("a.npy");
  const std::string npz = temp_path("b.npz");

  std::vector<double> d(6);
  for (size_t i = 0; i < d.size(); i++) d[i] = 1.5 * (double)i - 2.0;
  cnpy::npy_save(npy, d.data(), {2, 3}, "w");

  cnpy::NpyArray a = cnpy::npy_load(npy);
  check(a.shape == std::vector<size_t>({2, 3}), "npy shape");
  check(a.word_size == sizeof(double), "npy word size");
  check(!a.fortran_order, "npy order");
  check(a.num_vals == 6, "npy count");
  check(a.as_vec<double>() == d, "npy values");

  // Appending along the first axis grows the shape.
  cnpy::npy_save(npy, d.data(), {2, 3}, "a");
  cnpy::NpyArray ap = cnpy::npy_load(npy);
  check(ap.shape == std::vector<size_t>({4, 3}), "append shape");
  check(ap.as_vec<double>().back() == d.back(), "append values");

  std::vector<int> ints = {7, -8, 9, 100000};
  std::vector<float> floats = {0.25f, -4.5f};
  cnpy::npz_save(npz, "ints", ints, "w");
  cnpy::npz_save(npz, "floats", floats.data(), {2, 1}, "a");

  cnpy::npz_t all = cnpy::npz_load(npz);
  check(all.size() == 2, "npz entry count");
  check(all.count("ints") == 1 && all.count("floats") == 1, "npz names");
  check(all["ints"].shape == std::vector<size_t>({4}), "npz ints shape");
  check(all["ints"].as_vec<int>() == ints, "npz ints values");
  check(all["floats"].shape == std::vector<size_t>({2, 1}), "npz floats shape");
  check(all["floats"].word_size == sizeof(float), "npz floats word size");
  check(all["floats"].as_vec<float>() == floats, "npz floats values");
  check(cnpy::npz_load(npz, "ints").as_vec<int>() == ints, "npz_load by name");

  bool threw = false;
  try {
    cnpy::npz_load(npz, "missing");
  } catch (const std::runtime_error &) {
    threw = true;
  }
  check(threw, "missing npz variable throws");

  std::remove(npy.c_str());
  std::remove(npz.c_str());
  return failures == 0 ? 0 : 1;
}
