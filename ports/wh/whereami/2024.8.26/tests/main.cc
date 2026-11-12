// C++ consumer, in C++98: the C API through extern "C" from C++, filling
// std::vector buffers at the exact capacity and one byte short of it,
// splitting the result at dirname_length, and opening the executable it
// names with std::ifstream.
#include <whereami.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

// Calls the library from a second function, as an application would.
std::string executable_path(int* dirname_length) {
  int length = wai_getExecutablePath(NULL, 0, NULL);
  if (length <= 0)
    return std::string();
  std::vector<char> buffer(length);
  if (wai_getExecutablePath(&buffer[0], length, dirname_length) != length)
    return std::string();
  return std::string(buffer.begin(), buffer.end());
}

}  // namespace

int main() {
  int dirname_length = -1;
  const std::string path = executable_path(&dirname_length);
  check(!path.empty(), "the executable path is found");
  if (path.empty()) {
    std::printf("whereami (C++): FAILED\n");
    return 1;
  }
  check(path.find('\0') == std::string::npos, "the length excludes any NUL");
  if (dirname_length <= 0 || dirname_length >= static_cast<int>(path.size()) - 1) {
    std::printf("FAILED: dirname_length splits the path\nwhereami (C++): FAILED\n");
    return 1;
  }
  check(path[dirname_length] == '/' || path[dirname_length] == '\\',
        "dirname_length points at a separator");
  const std::string basename = path.substr(dirname_length + 1);
  check(basename.find('/') == std::string::npos && basename.find('\\') == std::string::npos,
        "the basename holds no separator");

  std::ifstream file(path.c_str(), std::ios::binary);
  check(file.good() && file.get() != std::char_traits<char>::eof(),
        "the executable can be opened and read");

  // One byte short: the required length comes back, dirname_length is left
  // alone, and nothing is written past the capacity (Windows may fill the
  // buffer itself).
  const int length = static_cast<int>(path.size());
  std::vector<char> short_buffer(length + 1, '#');
  int untouched = -5;
  check(wai_getExecutablePath(&short_buffer[0], length - 1, &untouched) == length,
        "a short buffer gets the required length");
  check(untouched == -5, "a short buffer leaves dirname_length alone");
  check(short_buffer[length - 1] == '#' && short_buffer[length] == '#',
        "nothing is written past the capacity");

  std::vector<char> module(length);
  check(wai_getModulePath(&module[0], length, NULL) == length &&
            std::string(module.begin(), module.end()) == path,
        "the module containing main() is the executable");

  std::printf("whereami (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
