#include <nudb/nudb.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what, const nudb::error_code& ec = {}) {
  if (!ok) {
    std::printf("FAILED: %s %s\n", what, ec.message().c_str());
    ++failures;
  }
}

std::string temp_base() {
  const char* dir = std::getenv("TMPDIR");
  if (!dir || !*dir) dir = std::getenv("TEMP");
  std::string base = dir && *dir ? dir : "/tmp";
  return base + "/nudb-consumer-" + std::to_string(std::random_device{}());
}

constexpr std::size_t key_size = 8;
constexpr std::uint64_t appnum = 7, uid = 0x1122334455667788ULL,
                        salt = 0x0123456789abcdefULL;

std::string key_for(unsigned i) {
  std::string k(key_size, '\0');
  std::memcpy(&k[0], "key", 3);
  k[4] = static_cast<char>('0' + i);
  return k;
}

std::string value_for(unsigned i) {
  // Different lengths, with an embedded NUL.
  return std::string(i * 13 + 1, static_cast<char>('a' + i)) + '\0' + "end";
}

}  // namespace

int main() {
  using namespace nudb;
  constexpr unsigned count = 6;
  const std::string base = temp_base();
  const std::string dat = base + ".dat", key = base + ".key",
                    log = base + ".log";
  error_code ec;

  create<xxhasher>(dat, key, log, appnum, uid, salt, key_size,
                   block_size("."), 0.5f, ec);
  check(!ec, "create", ec);

  store db;
  db.open(dat, key, log, ec);
  check(!ec, "open", ec);
  check(db.is_open(), "is_open");
  check(db.appnum() == appnum, "appnum");
  check(db.key_size() == key_size, "key_size");

  for (unsigned i = 0; i < count; ++i) {
    const std::string v = value_for(i);
    db.insert(key_for(i).data(), v.data(), v.size(), ec);
    check(!ec, "insert", ec);
  }

  // The documented duplicate-key error, which leaves the stored value alone.
  const std::string other = "other";
  db.insert(key_for(2).data(), other.data(), other.size(), ec);
  check(ec == error::key_exists, "duplicate insert gives key_exists", ec);
  check(ec.message() == "key already exists", "key_exists message");
  ec = {};

  auto fetch_all = [&](const char* when) {
    for (unsigned i = 0; i < count; ++i) {
      std::string got;
      db.fetch(key_for(i).data(),
               [&](const void* p, std::size_t n) {
                 got.assign(static_cast<const char*>(p), n);
               },
               ec);
      check(!ec, when, ec);
      check(got == value_for(i), when);
      ec = {};
    }
    const std::string missing = key_for(count + 1);
    bool called = false;
    db.fetch(missing.data(),
             [&](const void*, std::size_t) { called = true; }, ec);
    check(ec == error::key_not_found && !called, "missing key not found", ec);
    ec = {};
  };
  fetch_all("fetch before close");

  db.close(ec);
  check(!ec, "close", ec);
  check(!db.is_open(), "closed");

  db.open(dat, key, log, ec);
  check(!ec, "reopen", ec);
  fetch_all("fetch after reopen");
  db.close(ec);
  check(!ec, "close again", ec);

  verify_info info;
  verify<xxhasher>(info, dat, key, 0, no_progress{}, ec);
  check(!ec, "verify", ec);
  check(info.key_count == count && info.value_count == count, "verify counts");
  check(info.key_size == key_size && info.salt == salt && info.uid == uid &&
            info.appnum == appnum,
        "verify header fields");

  std::map<std::string, std::string> seen;
  visit(dat,
        [&](const void* k, std::size_t ks, const void* d, std::size_t ds,
            error_code&) {
          seen[std::string(static_cast<const char*>(k), ks)] =
              std::string(static_cast<const char*>(d), ds);
        },
        no_progress{}, ec);
  check(!ec, "visit", ec);
  check(seen.size() == count, "visit item count");
  for (unsigned i = 0; i < count; ++i)
    check(seen[key_for(i)] == value_for(i), "visit item bytes");

  erase_file(dat);
  erase_file(key);
  erase_file(log);

  if (failures == 0) std::puts("nudb consumer test passed");
  return failures == 0 ? 0 : 1;
}
