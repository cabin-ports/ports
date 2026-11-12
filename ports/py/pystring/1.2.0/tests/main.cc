// pystring: Python's str methods on std::string (split, strip, replace,
// partition, padding, case and slicing), and os.path for POSIX and Windows
// paths, including the variant chosen for the host.
#include <pystring/pystring.h>
// The build-tree spelling reaches the same guarded header.
#include "pystring.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

using strings = std::vector<std::string>;

}  // namespace

int main() {
  namespace path = pystring::os::path;

  strings parts;
  pystring::split("a,b,,c", parts, ",");
  CHECK((parts == strings{"a", "b", "", "c"}));
  pystring::split("  one \t two\nthree  ", parts);
  CHECK((parts == strings{"one", "two", "three"}));
  pystring::rsplit("a.b.c", parts, ".", 1);
  CHECK((parts == strings{"a.b", "c"}));
  pystring::splitlines("x\ny\r\nz", parts);
  CHECK((parts == strings{"x", "y", "z"}));
  pystring::partition("key=value=x", "=", parts);
  CHECK((parts == strings{"key", "=", "value=x"}));
  pystring::rpartition("key=value=x", "=", parts);
  CHECK((parts == strings{"key=value", "=", "x"}));

  CHECK(pystring::join(", ", strings{"a", "b", "c"}) == "a, b, c");
  CHECK(pystring::strip("  padded \n") == "padded");
  CHECK(pystring::strip("xxhixx", "x") == "hi");
  CHECK(pystring::rstrip("path///", "/") == "path");
  CHECK(pystring::replace("aaaa", "a", "b", 2) == "bbaa");
  CHECK(pystring::removeprefix("abcdef", "abc") == "def");
  CHECK(pystring::removesuffix("file.txt", ".txt") == "file");

  CHECK(pystring::count("banana", "an") == 2);
  CHECK(pystring::find("banana", "na", 3) == 4);
  CHECK(pystring::rfind("banana", "an") == 3);
  CHECK(pystring::find("banana", "x") == -1);
  CHECK(pystring::startswith("pystring", "py"));
  CHECK(pystring::endswith("archive.tar.gz", ".gz"));
  CHECK(!pystring::endswith("archive.tar.gz", ".tar"));

  CHECK(pystring::zfill("-42", 6) == "-00042");
  CHECK(pystring::center("ab", 6) == "  ab  ");
  CHECK(pystring::ljust("ab", 4) == "ab  ");
  CHECK(pystring::rjust("ab", 4) == "  ab");
  CHECK(pystring::expandtabs("a\tb", 4) == "a   b");
  CHECK(pystring::mul("ab", 3) == "ababab");

  CHECK(pystring::upper("MiXeD") == "MIXED");
  CHECK(pystring::lower("MiXeD") == "mixed");
  CHECK(pystring::swapcase("MiXeD") == "mIxEd");
  CHECK(pystring::capitalize("hELLO") == "Hello");
  CHECK(pystring::title("hello wide world") == "Hello Wide World");
  CHECK(pystring::istitle("Hello World") && !pystring::istitle("Hello world"));
  CHECK(pystring::isdigit("2024") && !pystring::isdigit("20x4"));
  CHECK(pystring::isalnum("abc123") && pystring::isspace(" \t\n"));

  CHECK(pystring::slice("abcdef", 2, -1) == "cde");
  CHECK(pystring::slice("abcdef", -2) == "ef");
  CHECK(pystring::slice("abcdef", -99) == "abcdef");

  // os.path on POSIX paths
  CHECK(path::join_posix("usr", "lib") == "usr/lib");
  CHECK(path::join_posix("usr", "/etc") == "/etc");
  CHECK(path::normpath_posix("A/foo/../B//./C") == "A/B/C");
  CHECK(path::abspath_posix("../tmp/a", "/home/user") == "/home/tmp/a");
  CHECK(path::basename_posix("/x/y.txt") == "y.txt");
  CHECK(path::dirname_posix("/x/y.txt") == "/x");
  CHECK(path::isabs_posix("/x") && !path::isabs_posix("x"));
  std::string root, ext;
  path::splitext_posix(root, ext, "dir/file.tar.gz");
  CHECK(root == "dir/file.tar" && ext == ".gz");

  // os.path on Windows paths
  CHECK(path::join_nt("c:\\a", "b") == "c:\\a\\b");
  CHECK(path::join_nt("c:\\a", "c:\\b") == "c:\\b");
  CHECK(path::normpath_nt("C:/A/foo/../B") == "C:\\A\\B");
  std::string drive, rest;
  path::splitdrive_nt(drive, rest, "C:\\Users\\test");
  CHECK(drive == "C:" && rest == "\\Users\\test");
  CHECK(path::basename_nt("C:\\x\\y.txt") == "y.txt");
  CHECK(path::isabs_nt("C:\\x") && !path::isabs_nt("x\\y"));

  // The unsuffixed functions follow the host.
#ifdef _WIN32
  CHECK(path::join("a", "b") == "a\\b");
  CHECK(path::normpath("a/./b") == "a\\b");
#else
  CHECK(path::join("a", "b") == "a/b");
  CHECK(path::normpath("a/./b") == "a/b");
#endif

  if (failures != 0) {
    return 1;
  }
  std::printf("pystring ok\n");
  return 0;
}
