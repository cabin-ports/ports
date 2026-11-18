// The C++ consumer's view of the C header: it lists a private temporary directory with known
// entries, unsorted and sorted, opens one file, and removes everything with the platform's own
// calls.  No symlinks, as in main.c.
#include <tinydir.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

int failures = 0;

#define CHECK(cond)                                                                          \
  do {                                                                                       \
    if (!(cond)) {                                                                           \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);          \
      ++failures;                                                                            \
    }                                                                                        \
  } while (0)

bool makeDir(const std::string &path) {
#ifdef _WIN32
  return CreateDirectoryA(path.c_str(), NULL) != 0;
#else
  return mkdir(path.c_str(), 0700) == 0;
#endif
}

bool removeDir(const std::string &path) {
#ifdef _WIN32
  return RemoveDirectoryA(path.c_str()) != 0;
#else
  return rmdir(path.c_str()) == 0;
#endif
}

bool removeFile(const std::string &path) {
#ifdef _WIN32
  return DeleteFileA(path.c_str()) != 0;
#else
  return unlink(path.c_str()) == 0;
#endif
}

bool writeFile(const std::string &path) {
  std::FILE *f = std::fopen(path.c_str(), "wb");
  if (f == NULL)
    return false;
  std::fputs("tinydir\n", f);
  return std::fclose(f) == 0;
}

std::string tempRoot() {
  std::ostringstream id;
#ifdef _WIN32
  char base[MAX_PATH + 1];
  DWORD len = GetTempPathA(static_cast<DWORD>(sizeof base), base);
  std::string root = len == 0 || len > MAX_PATH ? std::string(".") : std::string(base, len);
  id << GetCurrentProcessId();
#else
  const char *base = std::getenv("TMPDIR");
  std::string root = base && *base ? base : "/tmp";
  id << getpid();
#endif
  while (root.size() > 1 && (root[root.size() - 1] == '/' || root[root.size() - 1] == '\\'))
    root.erase(root.size() - 1);
  return root + "/tinydir-cxx-test-" + id.str();
}

// Name -> "d" for a directory, "f" for a regular file.
typedef std::map<std::string, std::string> Kinds;

std::string kind(const tinydir_file &f) { return f.is_dir ? "d" : f.is_reg ? "f" : "?"; }

}  // namespace

int main() {
  const std::string root = tempRoot();
  if (!makeDir(root)) {
    std::fprintf(stderr, "cannot create %s\n", root.c_str());
    return 1;
  }
  const std::string notes = root + "/notes.md", data = root + "/data", dir = root + "/dir";
  const bool built = writeFile(notes) && writeFile(data) && makeDir(dir);
  CHECK(built);

  if (built) {
    Kinds expected;
    expected["."] = "d";
    expected[".."] = "d";
    expected["dir"] = "d";
    expected["data"] = "f";
    expected["notes.md"] = "f";

    Kinds found;
    tinydir_dir d;
    CHECK(tinydir_open(&d, root.c_str()) == 0);
    while (d.has_next) {
      tinydir_file f;
      if (tinydir_readfile(&d, &f) != 0) {
        CHECK(false);
        break;
      }
      CHECK(f.path == root + "/" + f.name);
      found[f.name] += kind(f);
      CHECK(tinydir_next(&d) == 0);
    }
    tinydir_close(&d);
    CHECK(found == expected);

    // Sorted: directories first, then by name.
    const char *order[] = {".", "..", "dir", "data", "notes.md"};
    std::vector<std::string> names;
    CHECK(tinydir_open_sorted(&d, root.c_str()) == 0);
    for (size_t i = 0; i < d.n_files; ++i) {
      tinydir_file f;
      if (tinydir_readfile_n(&d, &f, i) == 0)
        names.push_back(f.name);
    }
    tinydir_close(&d);
    CHECK(names == std::vector<std::string>(order, order + 5));

    tinydir_file f;
    if (tinydir_file_open(&f, notes.c_str()) == 0) {
      CHECK(std::string(f.name) == "notes.md" && std::string(f.extension) == "md");
      CHECK(f.is_reg == 1 && f.is_dir == 0 && f.path == notes);
    } else {
      CHECK(false);
    }
    errno = 0;
    CHECK(tinydir_file_open(&f, (root + "/absent").c_str()) == -1 && errno == ENOENT);
  }

  CHECK(removeDir(dir));
  CHECK(removeFile(data));
  CHECK(removeFile(notes));
  CHECK(removeDir(root));

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("tinydir: ok\n");
  return 0;
}
