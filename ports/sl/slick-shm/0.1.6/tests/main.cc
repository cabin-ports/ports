// A segment under a per-run name is created, written, and read through a
// second, read-only mapping opened by name.  open_or_create attaches to it,
// a view survives a move of its owner, and errors arrive both as codes and as
// exceptions.  The segment is gone once every mapping is closed and removed.
#include <slick/shm/shared_memory.hpp>
#include <slick/shm/shared_memory_view.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <utility>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

namespace shm = slick::shm;

constexpr char greeting[] = "hello from cabin";
// A whole number of pages and of Windows' 64 KiB granularity, so every
// platform maps exactly this much.
constexpr std::size_t requested = std::size_t{1} << 20;

std::uint32_t read_counter(const void* base) {
  std::uint32_t value = 0;
  std::memcpy(&value, static_cast<const char*>(base) + 64, sizeof value);
  return value;
}

void write_counter(void* base, std::uint32_t value) {
  std::memcpy(static_cast<char*>(base) + 64, &value, sizeof value);
}

}  // namespace

int main() {
  // Short enough for macOS, whose POSIX shared memory names stop at 31 bytes.
  std::random_device device;
  const unsigned long long tag =
      (static_cast<unsigned long long>(device()) << 16) ^
      static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
  char name[32];
  std::snprintf(name, sizeof name, "cabin-shm-%012llx", tag & 0xffffffffffffULL);

  CHECK(!shm::shared_memory::exists(name));
  shm::shared_memory missing(name, shm::open_existing, shm::access_mode::read_only, std::nothrow);
  CHECK(!missing.is_valid());
  CHECK(missing.last_error() == shm::errc::not_found);
  try {
    shm::shared_memory thrown(name, shm::open_existing);
    CHECK(false);
  } catch (const shm::shared_memory_error& error) {
    CHECK(error.code() == shm::errc::not_found);
    CHECK(std::string(error.code().category().name()) == "slick-shm");
    CHECK(error.code().message() == "shared memory not found");
  }

  shm::shared_memory bad_name("a/b", requested, shm::create_only, shm::access_mode::read_write,
                              std::nothrow);
  CHECK(bad_name.last_error() == shm::errc::invalid_name);
  shm::shared_memory empty(name, 0, shm::create_only, shm::access_mode::read_write, std::nothrow);
  CHECK(empty.last_error() == shm::errc::invalid_size);
  CHECK(!shm::shared_memory::exists(name));

  shm::shared_memory writer(name, requested, shm::create_only);
  CHECK(writer.is_valid());
  CHECK(!writer.last_error());
  CHECK(writer.is_creator());
  CHECK(writer.mode() == shm::access_mode::read_write);
  CHECK(std::string(writer.name()) == name);
  CHECK(writer.size() == requested);
  CHECK(shm::shared_memory::exists(name));
  std::memcpy(writer.data(), greeting, sizeof greeting);
  write_counter(writer.data(), 7);

  shm::shared_memory again(name, requested, shm::create_only, shm::access_mode::read_write,
                           std::nothrow);
  CHECK(!again.is_valid());
  CHECK(again.last_error() == shm::errc::already_exists);

  shm::shared_memory reader(name, shm::open_existing, shm::access_mode::read_only);
  CHECK(reader.is_valid());
  CHECK(!reader.is_creator());
  CHECK(reader.mode() == shm::access_mode::read_only);
  CHECK(reader.data() != writer.data());
  CHECK(reader.size() == requested);
  CHECK(std::memcmp(reader.data(), greeting, sizeof greeting) == 0);
  CHECK(read_counter(reader.data()) == 7);
  write_counter(writer.data(), 8);
  CHECK(read_counter(reader.data()) == 8);

  shm::shared_memory attached(name, 4 * requested, shm::open_or_create);
  CHECK(attached.is_valid());
  CHECK(!attached.is_creator());
  CHECK(attached.size() == requested);
  write_counter(attached.data(), 9);
  CHECK(read_counter(reader.data()) == 9);

  const shm::shared_memory_view view(writer);
  const char* const name_storage = writer.name();
  shm::shared_memory moved(std::move(writer));
  CHECK(!writer.is_valid());
  CHECK(moved.is_valid());
  CHECK(moved.is_creator());
  CHECK(moved.name() == name_storage);
  shm::shared_memory_view copy = view;
  CHECK(copy.data() == moved.data());
  CHECK(copy.size() == moved.size());
  CHECK(std::strcmp(copy.name(), name) == 0);
  CHECK(std::strcmp(static_cast<const char*>(copy.data()), greeting) == 0);

  moved.unmap();
  CHECK(!moved.is_valid());
  moved.close();
  CHECK(moved.size() == 0);
  CHECK(std::memcmp(reader.data(), greeting, sizeof greeting) == 0);
  attached.close();
  reader.close();
  // shm_unlink on POSIX; on Windows the last closed handle already freed it.
  CHECK(shm::shared_memory::remove(name));
  CHECK(!shm::shared_memory::exists(name));

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("slick-shm C++ consumer test passed");
  return 0;
}
