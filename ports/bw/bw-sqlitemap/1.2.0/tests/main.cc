#include <bw/sqlitemap/sqlitemap.hpp>

#include <cstdio>
#include <filesystem>
#include <string>

static int failures = 0;
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

int main() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "cabin-bw-sqlitemap-test";
  fs::create_directories(dir);
  const std::string file = (dir / "t.sqlite").string();
  fs::remove(file);

  {
    bw::sqlitemap::sqlitemap db(file);
    db["a"] = "first";
    db.set("b", "second");
    db.set("b", "second2");  // set overwrites
    CHECK(db.size() == 2);
    CHECK(db.get("b") == "second2");
    std::string a = db["a"];
    CHECK(a == "first");

    db.emplace("a", "ignored");  // emplace does not overwrite
    db.emplace("c", "third");
    CHECK(db.get("a") == "first");
    CHECK(db.get("c") == "third");

    auto got = db.try_get("a");
    CHECK(got.has_value() && *got == "first");
    CHECK(!db.try_get("zzz").has_value());
    CHECK(db.contains("c") && !db.contains("zzz"));

    bool threw = false;
    try {
      db.get("zzz");
    } catch (const bw::sqlitemap::sqlitemap_error&) {
      threw = true;
    }
    CHECK(threw);

    db.del("c");
    CHECK(!db.contains("c"));
    CHECK(db.size() == 2);

    std::string joined;
    for (auto& [key, value] : db) joined += key + "=" + value + ";";
    CHECK(joined == "a=first;b=second2;");

    db.commit();
    db["d"] = "uncommitted";  // lost: close without commit
    db.close();
    db.connect();
    CHECK(db.size() == 2);
    CHECK(!db.contains("d"));
  }

  {  // a fresh connection sees the committed data
    bw::sqlitemap::sqlitemap db(file);
    CHECK(db.size() == 2);
    CHECK(db.get("a") == "first");
    CHECK(db.get("b") == "second2");
  }

  fs::remove_all(dir);
  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
