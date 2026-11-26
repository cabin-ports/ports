#include <mini/ini.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("cabin-ports-mini-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".ini");
  {
    std::ofstream out(path, std::ios::binary);
    out << "; comment\n[Fruits]\nApples = 20\noranges=30\n\n[veg]\ncarrot = orange\n";
  }

  mINI::INIFile file(path);
  mINI::INIStructure ini;
  CHECK(file.read(ini));

  // Keys and sections are case-insensitive by default; values keep their case.
  CHECK(ini.has("fruits"));
  CHECK(ini.has("FRUITS"));
  CHECK(ini["fruits"]["apples"] == "20");
  CHECK(ini["FRUITS"]["ORANGES"] == "30");
  CHECK(ini.get("veg").get("CARROT") == "orange");

  // Missing section and key.
  CHECK(!ini.has("nothing"));
  CHECK(!ini["fruits"].has("pears"));
  CHECK(ini.get("fruits").get("pears").empty());
  CHECK(ini.get("nothing").get("apples").empty());
  CHECK(ini.size() == 2);

  // Read-modify-write: write() updates the file in place to match the
  // structure, so the keys left untouched survive.
  ini["fruits"]["apples"] = "25";
  ini["fruits"]["pears"] = "7";
  CHECK(file.write(ini));

  mINI::INIStructure again;
  CHECK(file.read(again));
  CHECK(again["fruits"]["apples"] == "25");
  CHECK(again["fruits"]["pears"] == "7");
  CHECK(again["fruits"]["oranges"] == "30");
  CHECK(again["veg"]["carrot"] == "orange");
  CHECK(again.size() == 2);

  std::error_code ec;
  std::filesystem::remove(path, ec);
  CHECK(!std::filesystem::exists(path));
  return failures == 0 ? 0 : 1;
}
