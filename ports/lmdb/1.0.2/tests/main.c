/* Stores 100 records and a duplicate-sorted table in a fresh LMDB file,
 * reads them back through gets and cursors, rolls back an aborted write,
 * and reopens the file read-only. */
#include <lmdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/* Checks an LMDB return code, and says which call failed and why. */
static int ok(int rc, const char *what) {
  if (rc != MDB_SUCCESS) {
    fprintf(stderr, "FAIL: %s: %s\n", what, mdb_strerror(rc));
    ++failures;
  }
  return rc == MDB_SUCCESS;
}

static MDB_val text(const char *s) {
  MDB_val v;
  v.mv_size = strlen(s);
  v.mv_data = (void *)s;
  return v;
}

static int equals(const MDB_val *v, const char *s) {
  return v->mv_size == strlen(s) && memcmp(v->mv_data, s, v->mv_size) == 0;
}

static MDB_env *open_env(const char *path, unsigned flags) {
  MDB_env *env = NULL;
  if (!ok(mdb_env_create(&env), "mdb_env_create")) return NULL;
  ok(mdb_env_set_maxdbs(env, 4), "mdb_env_set_maxdbs");
  ok(mdb_env_set_mapsize(env, (size_t)16 << 20), "mdb_env_set_mapsize");
  if (!ok(mdb_env_open(env, path, MDB_NOSUBDIR | flags, 0644),
          "mdb_env_open")) {
    mdb_env_close(env);
    return NULL;
  }
  return env;
}

static void write_records(MDB_env *env) {
  MDB_txn *txn;
  MDB_dbi squares, tags;
  MDB_val k, v;
  char key[16], value[16];
  int i;

  if (!ok(mdb_txn_begin(env, NULL, 0, &txn), "mdb_txn_begin")) return;
  ok(mdb_dbi_open(txn, "squares", MDB_CREATE, &squares), "open squares");
  /* Descending, so the cursor's ascending order is LMDB's doing. */
  for (i = 99; i >= 0; --i) {
    snprintf(key, sizeof key, "key%03d", i);
    snprintf(value, sizeof value, "%d", i * i);
    k = text(key);
    v = text(value);
    ok(mdb_put(txn, squares, &k, &v, 0), "mdb_put");
  }
  k = text("key007");
  v = text("not 49");
  check(mdb_put(txn, squares, &k, &v, MDB_NOOVERWRITE) == MDB_KEYEXIST,
        "MDB_NOOVERWRITE keeps an existing key");
  check(equals(&v, "49"), "MDB_KEYEXIST returns the existing value");

  ok(mdb_dbi_open(txn, "tags", MDB_CREATE | MDB_DUPSORT, &tags), "open tags");
  k = text("fruit");
  v = text("pear");
  ok(mdb_put(txn, tags, &k, &v, 0), "put pear");
  v = text("apple");
  ok(mdb_put(txn, tags, &k, &v, 0), "put apple");
  v = text("fig");
  ok(mdb_put(txn, tags, &k, &v, 0), "put fig");
  v = text("apple");
  check(mdb_put(txn, tags, &k, &v, MDB_NODUPDATA) == MDB_KEYEXIST,
        "MDB_NODUPDATA rejects a duplicate pair");
  ok(mdb_txn_commit(txn), "commit");

  /* An aborted write leaves no trace. */
  if (ok(mdb_txn_begin(env, NULL, 0, &txn), "mdb_txn_begin")) {
    k = text("key100");
    v = text("10000");
    ok(mdb_put(txn, squares, &k, &v, 0), "put in aborted txn");
    k = text("key050");
    ok(mdb_del(txn, squares, &k, NULL), "del in aborted txn");
    mdb_txn_abort(txn);
  }
}

static void read_records(MDB_env *env) {
  MDB_txn *txn;
  MDB_dbi squares, tags;
  MDB_cursor *cur;
  MDB_val k, v;
  MDB_stat st;
  mdb_size_t dups = 0;
  char want[16];
  int i = 0, ordered = 1, rc;

  if (!ok(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn), "read txn")) return;
  ok(mdb_dbi_open(txn, "squares", 0, &squares), "reopen squares");
  check(mdb_dbi_open(txn, "missing", 0, &tags) == MDB_NOTFOUND,
        "a missing table is MDB_NOTFOUND without MDB_CREATE");
  k = text("key050");
  check(mdb_get(txn, squares, &k, &v) == 0 && equals(&v, "2500"), "mdb_get");
  k = text("key100");
  check(mdb_get(txn, squares, &k, &v) == MDB_NOTFOUND,
        "aborted put rolled back");
  ok(mdb_stat(txn, squares, &st), "mdb_stat");
  check(st.ms_entries == 100, "100 entries");

  ok(mdb_cursor_open(txn, squares, &cur), "mdb_cursor_open");
  for (rc = mdb_cursor_get(cur, &k, &v, MDB_FIRST); rc == 0;
       rc = mdb_cursor_get(cur, &k, &v, MDB_NEXT), ++i) {
    snprintf(want, sizeof want, "key%03d", i);
    ordered = ordered && equals(&k, want);
  }
  check(rc == MDB_NOTFOUND && i == 100 && ordered,
        "cursor walks keys in order");
  k = text("key04x");
  check(mdb_cursor_get(cur, &k, &v, MDB_SET_RANGE) == 0 &&
            equals(&k, "key050") && equals(&v, "2500"),
        "MDB_SET_RANGE finds the next key");
  mdb_cursor_close(cur);

  ok(mdb_dbi_open(txn, "tags", MDB_DUPSORT, &tags), "reopen tags");
  ok(mdb_cursor_open(txn, tags, &cur), "tags cursor");
  k = text("fruit");
  check(mdb_cursor_get(cur, &k, &v, MDB_SET_KEY) == 0 && equals(&v, "apple"),
        "duplicates sort");
  ok(mdb_cursor_count(cur, &dups), "mdb_cursor_count");
  check(dups == 3, "three duplicates");
  check(mdb_cursor_get(cur, &k, &v, MDB_NEXT_DUP) == 0 &&
            equals(&v, "fig") &&
            mdb_cursor_get(cur, &k, &v, MDB_NEXT_DUP) == 0 &&
            equals(&v, "pear") &&
            mdb_cursor_get(cur, &k, &v, MDB_NEXT_DUP) == MDB_NOTFOUND,
        "MDB_NEXT_DUP");
  mdb_cursor_close(cur);
  mdb_txn_abort(txn);
}

int main(void) {
  const char *dir = getenv("CABIN_PROFILE_DIR");
  char path[4096], lock[4096 + 8];
  int major = 0, minor = 0, patch = 0;
  MDB_env *env;

  check(strncmp(mdb_version(&major, &minor, &patch), "LMDB 1.0.2:", 11) == 0,
        "version string");
  check(major == 1 && minor == 0 && patch == 2, "version numbers");
  check(strstr(mdb_strerror(MDB_NOTFOUND), "MDB_NOTFOUND") != NULL,
        "mdb_strerror");
  {
    /* module.c's loader, on a module that does not exist. */
    MDB_crypto_funcs *funcs = NULL;
    char *err = NULL;
    check(mdb_modload("port-probe-lmdb-no-such-module", NULL, &funcs,
                      &err) == NULL &&
              err != NULL && funcs == NULL,
          "mdb_modload reports a missing module");
  }

  /* Among the probe's own build outputs: `cabin run` sets this and keeps
   * the caller's working directory. */
  if (dir == NULL) {
    fprintf(stderr, "FAIL: CABIN_PROFILE_DIR is not set\n");
    return 1;
  }
  snprintf(path, sizeof path, "%s/port-probe-lmdb-c.mdb", dir);
  snprintf(lock, sizeof lock, "%s-lock", path);
  remove(path);
  remove(lock);

  env = open_env(path, 0);
  if (env != NULL) {
    write_records(env);
    mdb_env_close(env);
  }
  env = open_env(path, MDB_RDONLY);
  if (env != NULL) {
    read_records(env);
    mdb_env_close(env);
  }

  /* MDB_NOSUBDIR off: the path must be an existing directory. */
  {
    MDB_env *bad = NULL;
    snprintf(lock, sizeof lock, "%s/no-such-dir", dir);
    if (ok(mdb_env_create(&bad), "mdb_env_create")) {
      check(mdb_env_open(bad, lock, 0, 0644) != MDB_SUCCESS,
            "opening a missing directory fails");
      mdb_env_close(bad);
    }
  }

  snprintf(lock, sizeof lock, "%s-lock", path);
  remove(path);
  remove(lock);
  if (failures != 0) return 1;
  puts("lmdb C consumer ok");
  return 0;
}
