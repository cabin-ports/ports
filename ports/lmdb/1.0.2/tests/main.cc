// Exercises LMDB 1.0's page encryption hooks with a toy cipher, set up
// through mdb_modsetup and read back through mdb_env_set_encrypt; nested
// transactions; integer keys loaded with MDB_APPEND; and a compacting
// copy of the environment.
#include <lmdb.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool ok(int rc, const char* what) {
  if (rc != MDB_SUCCESS) {
    std::fprintf(stderr, "FAIL: %s: %s\n", what, mdb_strerror(rc));
    ++failures;
  }
  return rc == MDB_SUCCESS;
}

// XOR with the key and the page's IV: not a real cipher, but the pages
// on disk differ from the pages in memory only through this function.
int xor_pages(const MDB_val* src, MDB_val* dst, const MDB_val* key, int) {
  const unsigned char* in = static_cast<const unsigned char*>(src->mv_data);
  unsigned char* out = static_cast<unsigned char*>(dst->mv_data);
  const unsigned char* k = static_cast<const unsigned char*>(key[0].mv_data);
  const unsigned char* iv = static_cast<const unsigned char*>(key[1].mv_data);
  for (size_t i = 0; i < src->mv_size; ++i)
    out[i] = in[i] ^ k[i % key[0].mv_size] ^ iv[i % key[1].mv_size];
  return 0;
}

const int kKeySize = 32;

// Spreads the passphrase over kKeySize bytes.
int derive(const char* passphrase, MDB_val* key) {
  unsigned char* out = static_cast<unsigned char*>(key->mv_data);
  size_t n = std::strlen(passphrase);
  for (int i = 0; i < kKeySize; ++i)
    out[i] = static_cast<unsigned char>(passphrase[i % n] + 31 * i);
  return 0;
}

const char kPassphrase[] = "cabin ports";

// Views a string that must outlive the MDB_val: a literal, or a named
// std::string (a temporary would dangle before LMDB reads it).
MDB_val bytes(const char* s) {
  MDB_val v;
  v.mv_size = std::strlen(s);
  v.mv_data = const_cast<char*>(s);
  return v;
}

MDB_val bytes(const std::string& s) {
  MDB_val v;
  v.mv_size = s.size();
  v.mv_data = const_cast<char*>(s.data());
  return v;
}

std::string str(const MDB_val& v) {
  return std::string(static_cast<const char*>(v.mv_data), v.mv_size);
}

enum Crypto { kPlain, kModsetup, kSetEncrypt };

MDB_env* open_env(const std::string& path, unsigned flags, Crypto crypto) {
  MDB_env* env = nullptr;
  if (!ok(mdb_env_create(&env), "mdb_env_create")) return nullptr;
  ok(mdb_env_set_maxdbs(env, 2), "mdb_env_set_maxdbs");
  ok(mdb_env_set_mapsize(env, size_t(32) << 20), "mdb_env_set_mapsize");
  if (crypto == kModsetup) {
    // What the mdb_* tools do with a module's hooks (module.c).
    MDB_crypto_funcs funcs = {derive, xor_pages, nullptr, kKeySize, 0, 0};
    mdb_modsetup(env, &funcs, kPassphrase);
  } else if (crypto == kSetEncrypt) {
    unsigned char derived[kKeySize];
    MDB_val key;
    key.mv_size = sizeof derived;
    key.mv_data = derived;
    derive(kPassphrase, &key);
    ok(mdb_env_set_encrypt(env, xor_pages, &key, 0), "mdb_env_set_encrypt");
  }
  if (!ok(mdb_env_open(env, path.c_str(), MDB_NOSUBDIR | flags, 0644),
          "mdb_env_open")) {
    mdb_env_close(env);
    return nullptr;
  }
  return env;
}

void remove_env(const std::string& path) {
  std::remove(path.c_str());
  std::remove((path + "-lock").c_str());
}

const std::string kMarker = "a plaintext marker that must not hit the disk";

void encrypted(const std::string& path) {
  remove_env(path);
  if (MDB_env* env = open_env(path, 0, kModsetup)) {
    MDB_txn* txn;
    MDB_dbi dbi;
    if (ok(mdb_txn_begin(env, nullptr, 0, &txn), "mdb_txn_begin")) {
      ok(mdb_dbi_open(txn, nullptr, 0, &dbi), "mdb_dbi_open");
      MDB_val k = bytes("marker"), v = bytes(kMarker);
      ok(mdb_put(txn, dbi, &k, &v, 0), "mdb_put");
      ok(mdb_txn_commit(txn), "mdb_txn_commit");
    }
    mdb_env_close(env);
  }
  {
    // Closed again before the removal below, which Windows would refuse.
    std::ifstream file(path.c_str(), std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
    check(raw.size() > 0 && raw.find(kMarker) == std::string::npos,
          "the encrypted file hides the value");
  }

  if (MDB_env* env = open_env(path, MDB_RDONLY, kSetEncrypt)) {
    MDB_txn* txn;
    MDB_dbi dbi;
    if (ok(mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn), "read txn")) {
      ok(mdb_dbi_open(txn, nullptr, 0, &dbi), "mdb_dbi_open");
      MDB_val k = bytes("marker"), v;
      check(mdb_get(txn, dbi, &k, &v) == 0 && str(v) == kMarker,
            "the value decrypts");
      mdb_txn_abort(txn);
    }
    mdb_env_close(env);
  }
  remove_env(path);
}

void integers_nested_and_copy(const std::string& path,
                              const std::string& copy) {
  remove_env(path);
  remove_env(copy);
  MDB_env* env = open_env(path, 0, kPlain);
  if (env == nullptr) return;
  MDB_txn* txn;
  MDB_dbi ids;
  if (ok(mdb_txn_begin(env, nullptr, 0, &txn), "mdb_txn_begin")) {
    ok(mdb_dbi_open(txn, "ids", MDB_CREATE | MDB_INTEGERKEY, &ids), "open ids");
    for (size_t id = 1; id <= 1000; ++id) {
      std::string value = "row " + std::to_string(id);
      MDB_val k, v = bytes(value);
      k.mv_size = sizeof id;
      k.mv_data = &id;
      ok(mdb_put(txn, ids, &k, &v, MDB_APPEND), "MDB_APPEND");
    }
    size_t early = 5;
    MDB_val k, v = bytes("late");
    k.mv_size = sizeof early;
    k.mv_data = &early;
    check(mdb_put(txn, ids, &k, &v, MDB_APPEND) == MDB_KEYEXIST,
          "MDB_APPEND rejects an out-of-order key");

    // A child transaction's writes vanish with it; the parent's stay.
    MDB_txn* child;
    if (ok(mdb_txn_begin(env, txn, 0, &child), "nested txn")) {
      size_t id = 500;
      k.mv_data = &id;
      ok(mdb_del(child, ids, &k, nullptr), "del in child");
      check(mdb_get(child, ids, &k, &v) == MDB_NOTFOUND,
            "child sees its delete");
      mdb_txn_abort(child);
      check(mdb_get(txn, ids, &k, &v) == 0 && str(v) == "row 500",
            "parent unaffected by the aborted child");
    }
    if (ok(mdb_txn_begin(env, txn, 0, &child), "nested txn")) {
      size_t id = 1;
      k.mv_data = &id;
      ok(mdb_del(child, ids, &k, nullptr), "del in child");
      ok(mdb_txn_commit(child), "child commit");
      check(mdb_get(txn, ids, &k, &v) == MDB_NOTFOUND,
            "parent sees the committed child");
    }
    ok(mdb_txn_commit(txn), "mdb_txn_commit");
  }

  ok(mdb_env_copy2(env, copy.c_str(), MDB_CP_COMPACT), "mdb_env_copy2");
  mdb_env_close(env);

  if (MDB_env* again = open_env(copy, MDB_RDONLY, kPlain)) {
    if (ok(mdb_txn_begin(again, nullptr, MDB_RDONLY, &txn), "read copy")) {
      ok(mdb_dbi_open(txn, "ids", MDB_INTEGERKEY, &ids), "open ids in copy");
      MDB_stat st;
      ok(mdb_stat(txn, ids, &st), "mdb_stat");
      check(st.ms_entries == 999, "999 rows in the copy");
      MDB_cursor* cur;
      if (ok(mdb_cursor_open(txn, ids, &cur), "cursor")) {
        MDB_val k, v;
        size_t first = 0, last = 0;
        if (mdb_cursor_get(cur, &k, &v, MDB_FIRST) == 0)
          std::memcpy(&first, k.mv_data, sizeof first);
        if (mdb_cursor_get(cur, &k, &v, MDB_LAST) == 0)
          std::memcpy(&last, k.mv_data, sizeof last);
        check(first == 2 && last == 1000 && str(v) == "row 1000",
              "integer keys sort numerically");
        mdb_cursor_close(cur);
      }
      mdb_txn_abort(txn);
    }
    mdb_env_close(again);
  }
  remove_env(path);
  remove_env(copy);
}

}  // namespace

int main() {
  // Among the probe's own build outputs: `cabin run` sets this and keeps
  // the caller's working directory.
  const char* dir = std::getenv("CABIN_PROFILE_DIR");
  if (dir == nullptr) {
    std::fprintf(stderr, "FAIL: CABIN_PROFILE_DIR is not set\n");
    return 1;
  }
  const std::string base = std::string(dir) + "/port-probe-lmdb-cc";
  encrypted(base + "-enc.mdb");
  integers_nested_and_copy(base + ".mdb", base + "-copy.mdb");
  if (failures != 0) return 1;
  std::puts("lmdb C++ consumer ok");
  return 0;
}
