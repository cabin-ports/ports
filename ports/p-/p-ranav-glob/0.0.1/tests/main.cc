#include <glob/glob.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The sorted filenames of `found`, each of which must sit in `dir`.
static std::vector<std::string> names_in(const std::vector<fs::path>& found, const fs::path& dir) {
    std::vector<std::string> names;
    for (const auto& path : found) {
        check(path.parent_path() == dir, "match directory");
        names.push_back(path.filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

using names = std::vector<std::string>;

int main() {
    // Among the probe's own build outputs: `cabin run` keeps the caller's
    // working directory.
    const char* const profile = std::getenv("CABIN_PROFILE_DIR");
    check(profile != nullptr, "CABIN_PROFILE_DIR");
    if (profile == nullptr)
        return 1;
    const fs::path dir = fs::path(profile) / "port-probe-glob";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub" / "deep");
    for (const char* file : {"a.txt", "b.txt", "c.png", "sub/d.txt", "sub/deep/e.txt"})
        std::ofstream(dir / file) << file;
    const std::string root = dir.string();

    check(names_in(glob::glob(root + "/*.txt"), dir) == names{"a.txt", "b.txt"}, "*.txt");
    check(names_in(glob::glob(root + "/[ab].txt"), dir) == names{"a.txt", "b.txt"}, "[ab].txt");
    check(names_in(glob::glob(root + "/?.png"), dir) == names{"c.png"}, "?.png");
    check(names_in(glob::glob({root + "/*.txt", root + "/*.png"}), dir) == names{"a.txt", "b.txt", "c.png"},
          "two patterns");
    check(names_in(glob::rglob(root + "/**/e.txt"), dir / "sub" / "deep") == names{"e.txt"}, "rglob **/e.txt");
    check(glob::glob(root + "/*.none").empty(), "no match");
    const fs::path cwd = fs::current_path();
    fs::current_path(dir);
    check(names_in(glob::glob("*.txt"), fs::path{}) == names{"a.txt", "b.txt"}, "relative *.txt");
    fs::current_path(cwd);

    check(fs::remove_all(dir) == 8, "remove the directory");
    if (failures != 0)
        return 1;
    std::puts("glob consumer test passed");
    return 0;
}
