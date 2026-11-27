#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>
#include <cppfs/FilePath.h>
#include <cppfs/FileWatcher.h>
#include <cppfs/Tree.h>
#include <cppfs/Url.h>
#include <cppfs/fs.h>

#define CHECK(cond)                                                         \
  do {                                                                      \
    if (!(cond)) {                                                          \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                   #cond);                                                  \
      return 1;                                                             \
    }                                                                       \
  } while (0)

static std::string tempBase() {
  for (const char* name : {"TMPDIR", "TEMP", "TMP"}) {
    if (const char* dir = std::getenv(name)) {
      if (*dir) return dir;
    }
  }
  return "/tmp";
}

int main() {
  // FilePath: unified separators, parts and resolution.
  cppfs::FilePath path("/usr/local/../bin/./tool.tar.gz");
  CHECK(path.isAbsolute());
  CHECK(path.fileName() == "tool.tar.gz");
  CHECK(path.baseName() == "tool");
  CHECK(path.extension() == ".tar.gz");
  CHECK(path.directoryPath() == "/usr/local/../bin/./");
  CHECK(path.resolved() == "/usr/bin/tool.tar.gz");
  CHECK(cppfs::FilePath("a\\b\\c.txt").path() == "a/b/c.txt");
  CHECK(cppfs::FilePath("C:/dir/f.c").driveLetter() == "C:");
  CHECK(cppfs::FilePath("../x/../../y").resolved() == "../../y");
  CHECK(cppfs::FilePath("base/dir").resolve("file.txt").path() ==
        "base/dir/file.txt");
  CHECK(cppfs::FilePath("/abs").resolve(cppfs::FilePath("rel")).path() ==
        "/abs/rel");

  // Url
  cppfs::Url url("ssh://user:pw@host/dir/file");
  CHECK(url.protocol() == "ssh://");
  CHECK(url.username() == "user");
  CHECK(url.password() == "pw");
  CHECK(url.host() == "host");
  CHECK(url.path() == "/dir/file");

  // Base64, through the vendored basen.
  CHECK(cppfs::fs::base64("cppfs") == "Y3BwZnM");
  CHECK(cppfs::fs::fromBase64("Y3BwZnM") == "cppfs");

  // Without the ssh backend: no remote handle and no SHA-1 (see cabin.toml).
  CHECK(!cppfs::fs::open("ssh://user@host/dir").exists());
  CHECK(cppfs::fs::sha1("abc").empty());

  // A fresh directory under the system temp directory.
  std::string base = tempBase();
  std::replace(base.begin(), base.end(), '\\', '/');
  const std::string root =
      base + "/cppfs-test-" +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count());
  cppfs::FileHandle dir = cppfs::fs::open(root);
  CHECK(!dir.exists());
  CHECK(dir.createDirectory());
  CHECK(dir.exists() && dir.isDirectory() && !dir.isFile());

  // Write, read back, size and existence.
  cppfs::FileHandle file = dir.open("a.txt");
  CHECK(!file.exists());
  CHECK(file.writeFile("hello cppfs"));
  CHECK(file.exists() && file.isFile());
  CHECK(file.size() == 11);
  CHECK(file.fileName() == "a.txt");
  CHECK(file.readFile() == "hello cppfs");

  // Streams.
  {
    std::unique_ptr<std::ostream> out =
        file.createOutputStream(std::ios_base::out | std::ios_base::binary);
    CHECK(out != nullptr);
    *out << "line one" << ' ' << 42;
  }
  {
    std::unique_ptr<std::istream> in =
        file.createInputStream(std::ios_base::in | std::ios_base::binary);
    CHECK(in != nullptr);
    std::string word;
    int number = 0;
    *in >> word >> word >> number;
    CHECK(word == "one" && number == 42);
  }
  CHECK(file.readFile() == "line one 42");

  // Listing.
  cppfs::FileHandle sub = dir.open("sub");
  CHECK(sub.createDirectory());
  CHECK(dir.open("b.txt").writeFile("bbb"));
  std::vector<std::string> names = dir.listFiles();
  std::sort(names.begin(), names.end());
  CHECK((names == std::vector<std::string>{"a.txt", "b.txt", "sub"}));
  std::size_t count = 0;
  for (cppfs::FileIterator it = dir.begin(); it != dir.end(); ++it) ++count;
  CHECK(count == 3);

  // Copy, rename, move and remove.
  cppfs::FileHandle copy = sub.open("copy.txt");
  CHECK(file.copy(copy));
  CHECK(copy.readFile() == "line one 42" && file.exists());
  CHECK(copy.rename("renamed.txt"));
  CHECK(sub.open("renamed.txt").exists() && !sub.open("copy.txt").exists());
  cppfs::FileHandle moved = dir.open("moved.txt");
  cppfs::FileHandle renamed = sub.open("renamed.txt");
  CHECK(renamed.move(moved));
  CHECK(moved.readFile() == "line one 42");
  CHECK(!sub.open("renamed.txt").exists());

  // Directory trees: read, copy recursively and remove.
  CHECK(sub.open("deep.txt").writeFile("d"));
  std::unique_ptr<cppfs::Tree> tree = dir.readTree();
  CHECK(tree != nullptr && tree->children().size() == 4);
  cppfs::FileHandle dup = cppfs::fs::open(root + "-copy");
  CHECK(dup.createDirectory());
  dir.copyDirectoryRec(dup);
  CHECK(dup.open("sub/deep.txt").readFile() == "d");
  CHECK(dup.open("a.txt").readFile() == "line one 42");
  dup.removeDirectoryRec();
  CHECK(!dup.exists());

#ifdef __linux__
  // The inotify watcher, which only Linux builds.
  {
    int created = 0;
    cppfs::FileWatcher watcher = dir.watch(cppfs::FileCreated);
    watcher.addHandler([&](cppfs::FileHandle& fh, cppfs::FileEvent event) {
      if (event == cppfs::FileCreated && fh.fileName() == "new.txt") ++created;
    });
    CHECK(dir.open("new.txt").writeFile("n"));
    watcher.watch(2000);
    CHECK(created == 1);
  }
#endif

  dir.removeDirectoryRec();
  CHECK(!dir.exists());
  std::cout << "cppfs ok\n";
  return 0;
}
