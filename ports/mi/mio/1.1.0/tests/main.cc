// mio: a file written in the temporary directory is mapped whole, at an
// offset inside a page, shared and through unsigned bytes, then written
// through a sink across a page boundary; bad requests report errors.
#include <mio/mmap.hpp>
#include <mio/shared_mmap.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

// Creates a new file with a unique name in the temporary directory and opens
// it for writing.  Wide paths on Windows, so a non-ASCII temporary directory
// still works and mio's wchar_t overloads are the ones exercised there.
#ifdef _WIN32
using path_t = std::wstring;

std::FILE* create_temp_file(path_t& path) {
  wchar_t dir[MAX_PATH + 1];
  wchar_t name[MAX_PATH];
  if (GetTempPathW(MAX_PATH + 1, dir) == 0 ||
      GetTempFileNameW(dir, L"mio", 0, name) == 0) {
    return nullptr;
  }
  path = name;
  return _wfopen(name, L"wb");
}

int remove_file(const path_t& path) { return _wremove(path.c_str()); }

path_t missing(const path_t& path) { return path + L".missing"; }
#else
using path_t = std::string;

std::FILE* create_temp_file(path_t& path) {
  const char* dir = std::getenv("TMPDIR");
  path = std::string(dir && *dir ? dir : "/tmp") + "/mio-test-XXXXXX";
  const int fd = mkstemp(&path[0]);
  return fd < 0 ? nullptr : fdopen(fd, "wb");
}

int remove_file(const path_t& path) { return std::remove(path.c_str()); }

path_t missing(const path_t& path) { return path + ".missing"; }
#endif

}  // namespace

int main() {
  path_t path;
  std::FILE* const file = create_temp_file(path);
  if (!file) {
    std::fprintf(stderr, "main.cc: cannot create the test file\n");
    return 1;
  }

  const std::size_t page = mio::page_size();
  std::string text(2 * page + 123, '\0');
  for (std::size_t i = 0; i < text.size(); ++i) {
    text[i] = static_cast<char>('a' + i % 26);
  }

  const bool written =
      std::fwrite(text.data(), 1, text.size(), file) == text.size();
  if (std::fclose(file) != 0 || !written) {
    std::fprintf(stderr, "main.cc: cannot write the test file\n");
    return 1;
  }

  std::error_code error;
  {
    const mio::mmap_source whole = mio::make_mmap_source(path, error);
    CHECK(!error && whole.is_open() && whole.is_mapped());
    CHECK(whole.size() == text.size());
    CHECK(std::equal(whole.begin(), whole.end(), text.begin()));

    // mio maps from the page boundary below the offset and hides the rest.
    const std::size_t offset = page + 7;
    mio::mmap_source part{};
    part.map(path, offset, 100, error);
    CHECK(!error && part.size() == 100);
    CHECK(part.mapping_offset() == 7 && part.mapped_length() == 107);
    CHECK(std::equal(part.begin(), part.end(), text.begin() + offset));

    mio::shared_mmap_source shared(std::move(part));
    CHECK(!part.is_open() && shared.is_open());
    mio::shared_mmap_source copy = shared;
    CHECK(copy.get_shared_ptr() == shared.get_shared_ptr());
    CHECK(copy.size() == 100 && copy[0] == text[offset]);

    const mio::ummap_source bytes(path, 0, 4);
    CHECK(bytes.size() == 4 && bytes[0] == 'a' && bytes[3] == 'd');

    // Through the open handle: a failed map() by path leaks the handle it
    // opened, which on Windows would keep the file from being removed.
    mio::mmap_source past_end{};
    past_end.map(whole.file_handle(), text.size(), 1, error);
    CHECK(error == std::errc::invalid_argument && !past_end.is_mapped());
  }

  // A write that straddles the first page boundary, flushed to the file.
  {
    mio::mmap_sink sink = mio::make_mmap_sink(path, page - 2, 4, error);
    CHECK(!error && sink.size() == 4);
    if (!error) {
      std::memcpy(sink.data(), "MIO!", 4);
      sink.sync(error);
      CHECK(!error);
    }
  }
  {
    const mio::mmap_source after = mio::make_mmap_source(path, error);
    CHECK(!error && after.size() == text.size());
    if (!error) {
      CHECK(std::memcmp(after.data() + page - 2, "MIO!", 4) == 0);
      CHECK(after[page - 3] == text[page - 3] &&
            after[page + 2] == text[page + 2]);
    }
  }

  // Value-initialized and mapped in place: basic_mmap leaves
  // is_handle_internal_ uninitialized, and a make_mmap factory may move a
  // failed mapping out, which reads it.
  {
    mio::mmap_source bad{};
    bad.map(missing(path), error);
    CHECK(error && !bad.is_open());
    bad.map(path_t(), error);
    CHECK(error == std::errc::invalid_argument && !bad.is_open());
  }

  CHECK(remove_file(path) == 0);

  if (failures != 0) {
    return 1;
  }
  std::printf("mio ok\n");
  return 0;
}
