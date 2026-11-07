// ghc::filesystem from C++11, header-only: lexical path operations and the
// UTF-8/UTF-16/UTF-32 conversions, then real file operations in a fresh
// directory under temp_directory_path(), through both the throwing and the
// error_code overloads.  u8arguments makes Windows links reach shell32.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <set>
#include <string>
#include <system_error>

#include <ghc/filesystem.hpp>

namespace fs = ghc::filesystem;

static_assert(GHC_FILESYSTEM_VERSION == 10516L, "header version");

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

void lexical() {
  // Redundant separators are dropped on construction (README.md:606-608).
  const fs::path archive("pkg/./lib//libz.tar.gz");
  CHECK(archive.generic_string() == "pkg/./lib/libz.tar.gz");
  CHECK(archive.filename().string() == "libz.tar.gz");
  CHECK(archive.stem().string() == "libz.tar");
  CHECK(archive.extension().string() == ".gz");
  CHECK(archive.parent_path().generic_string() == "pkg/./lib");
  CHECK(archive.lexically_normal().generic_string() == "pkg/lib/libz.tar.gz");
  CHECK(fs::path("a/b/../../c/").lexically_normal().generic_string() == "c/");
  CHECK(fs::path("a/b/c").lexically_relative("a/d").generic_string() ==
        "../b/c");
  CHECK((fs::path("x") / "y" / "z.txt").generic_string() == "x/y/z.txt");
  fs::path doc("notes/doc.txt");
  doc.replace_extension(".md");
  CHECK(doc.generic_string() == "notes/doc.md");
  int parts = 0;
  for (const fs::path& part : fs::path("a/b/c")) {
    CHECK(part.string().size() == 1);
    ++parts;
  }
  CHECK(parts == 3);
#ifdef _WIN32
  CHECK(fs::path::preferred_separator == L'\\');
  const fs::path mixed("C:\\dir/file.txt");
  CHECK(mixed.native() == L"C:\\dir\\file.txt");
  CHECK(mixed.generic_string() == "C:/dir/file.txt");
  CHECK(mixed.is_absolute() && mixed.root_name().string() == "C:");
#else
  CHECK(fs::path::preferred_separator == '/');
  CHECK(fs::path("/usr/lib").is_absolute());
  CHECK(fs::path("/usr/lib").root_directory().string() == "/");
#endif
  CHECK(fs::path("a/b").is_relative());
}

// "€-日本.txt"; none of its characters decompose, so every file system
// stores the name as written.
const std::string kUnicodeName = "\xe2\x82\xac-\xe6\x97\xa5\xe6\x9c\xac.txt";

void unicode() {
  // std::string is UTF-8 on every platform (README.md:551-559).
  const fs::path name = fs::u8path(kUnicodeName);
  CHECK(name.u8string() == kUnicodeName);
  CHECK(name.wstring() == L"\u20ac-\u65e5\u672c.txt");
  CHECK(name.u16string() == u"\u20ac-\u65e5\u672c.txt");
  CHECK(name.u32string() == U"\u20ac-\u65e5\u672c.txt");
  const fs::path emoji(U"\U0001F600");
  CHECK(emoji.u8string() == "\xf0\x9f\x98\x80");
  CHECK(emoji.u16string() == u"\U0001F600");
  // Invalid UTF-8 becomes U+FFFD unless GHC_RAISE_UNICODE_ERRORS is defined
  // (filesystem.hpp:1584-1598).
  CHECK(fs::path(std::string(1, '\xff')).u16string() == u"\ufffd");
}

void files() {
  // create_directory returns false for an existing directory, so the tree
  // removed at the end is always one this run created.
  const std::string suffix = std::to_string(static_cast<long long>(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::path base;
  int attempt = 0;
  do {
    base = fs::temp_directory_path() /
           ("ghc-filesystem-test-" + suffix + "-" + std::to_string(attempt));
  } while (!fs::create_directory(base) && ++attempt < 100);
  if (attempt == 100) {
    check(false, __LINE__, "no fresh directory under temp_directory_path()");
    return;
  }
  std::error_code ec;
  CHECK(fs::create_directories(base / "sub" / "deep"));
  CHECK(fs::is_directory(base / "sub" / "deep"));
  CHECK(!fs::create_directory(base / "sub", ec) && !ec);

  const fs::path hello = base / "sub" / "hello.txt";
  {
    fs::ofstream out(hello, std::ios::binary);
    out << "hello, ghc\n";
    CHECK(out.good());
  }
  CHECK(fs::is_regular_file(hello));
  CHECK(fs::file_size(hello) == 11);
  {
    fs::ifstream in(hello, std::ios::binary);
    std::string line;
    CHECK(static_cast<bool>(std::getline(in, line)) && line == "hello, ghc");
  }

  const fs::path wide = base / "sub" / fs::u8path(kUnicodeName);
  {
    fs::ofstream out(wide, std::ios::binary);
    out << "x";
  }
  CHECK(fs::exists(wide) && fs::file_size(wide) == 1);

  std::set<std::string> names;
  for (const fs::directory_entry& entry : fs::directory_iterator(base / "sub")) {
    names.insert(entry.path().filename().u8string());
    CHECK(entry.is_directory() == (entry.path().filename() == "deep"));
  }
  CHECK(names == (std::set<std::string>{"deep", "hello.txt", kUnicodeName}));
  int entries = 0;
  for (fs::recursive_directory_iterator it(base), end; it != end; ++it) {
    ++entries;
  }
  CHECK(entries == 4);  // sub, sub/deep, and the two files

  const fs::path copy = base / "copy.txt";
  CHECK(fs::copy_file(hello, copy));
  CHECK(fs::file_size(copy) == 11);
  CHECK(!fs::copy_file(hello, copy, ec) && ec == std::errc::file_exists);
  CHECK(fs::copy_file(hello, copy, fs::copy_options::overwrite_existing));
  const fs::path moved = base / "moved.txt";
  fs::rename(copy, moved);
  CHECK(!fs::exists(copy) && fs::exists(moved));
  fs::resize_file(moved, 5);
  CHECK(fs::file_size(moved) == 5);

  const fs::file_time_type stamp =
      fs::last_write_time(hello) - std::chrono::hours(1);
  fs::last_write_time(hello, stamp);
  const auto drift = fs::last_write_time(hello) - stamp;
  CHECK(drift < std::chrono::seconds(1) && drift > -std::chrono::seconds(1));

#ifndef _WIN32
  // Windows permissions are synthesized (README.md:643-653).
  fs::permissions(hello, fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace);
  CHECK(fs::status(hello).permissions() ==
        (fs::perms::owner_read | fs::perms::owner_write));
#endif

  const fs::path missing = base / "missing";
  CHECK(fs::status(missing).type() == fs::file_type::not_found);
  CHECK(!fs::exists(missing, ec) && !ec);
  CHECK(fs::file_size(missing, ec) == static_cast<std::uintmax_t>(-1) &&
        ec == std::errc::no_such_file_or_directory);
  bool threw = false;
  try {
    fs::file_size(missing);
  } catch (const fs::filesystem_error& e) {
    threw = e.path1() == missing &&
            e.code() == std::errc::no_such_file_or_directory;
  }
  CHECK(threw);

  const fs::path original = fs::current_path();
  fs::current_path(base / "sub");
  CHECK(fs::equivalent(fs::current_path(), base / "sub"));
  CHECK(fs::is_regular_file("hello.txt"));
  CHECK(fs::equivalent(fs::absolute("deep"), base / "sub" / "deep"));
  fs::current_path(original);
  CHECK(fs::canonical(base / "sub" / ".." / "sub" / "deep") ==
        fs::canonical(base / "sub" / "deep"));
  const fs::space_info space = fs::space(base);
  CHECK(space.capacity > 0 && space.available <= space.capacity);

  CHECK(fs::remove(moved));
  CHECK(!fs::remove(moved));
  CHECK(fs::remove_all(base) == 5);  // base, sub, deep and the two files
  CHECK(!fs::exists(base));
}

}  // namespace

int main(int argc, char** argv) {
  {
    fs::u8arguments guard(argc, argv);
#ifdef _WIN32
    // argv rebuilt as UTF-8 from CommandLineToArgvW; elsewhere valid()
    // reports whether the locale's codeset is UTF-8.
    CHECK(guard.valid());
#endif
    CHECK(argc >= 1 && argv[0] != nullptr && argv[0][0] != '\0');
  }
  try {
    lexical();
    unicode();
    files();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "unexpected exception: %s\n", e.what());
    return 1;
  }
  if (failures != 0) {
    return 1;
  }
  std::printf("ghc::filesystem %ld.%ld.%ld: C++ API OK\n",
              GHC_FILESYSTEM_VERSION / 10000, GHC_FILESYSTEM_VERSION / 100 % 100,
              GHC_FILESYSTEM_VERSION % 100);
  return 0;
}
