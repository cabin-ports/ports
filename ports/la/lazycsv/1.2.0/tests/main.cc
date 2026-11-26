#include <lazycsv.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

using rows_t = std::vector<std::vector<std::string>>;

template <typename Parser>
rows_t trimmed_rows(const Parser &p) {
  rows_t rows;
  for (const auto row : p) {
    rows.emplace_back();
    for (const auto cell : row) rows.back().emplace_back(cell.trimmed());
  }
  return rows;
}

}  // namespace

int main() {
  const std::string text =
      "name,note\n"
      "alice,\"hi, there\"\n"
      "bob,\"say \"\"x\"\"\"\n"
      " carol ,\t3";

  // Header handling: the first row is the header and is skipped by iteration.
  lazycsv::parser<std::string> with_header{text};
  check(with_header.header().raw() == "name,note", "header raw");
  check(with_header.index_of("note") == 1, "index_of note");
  bool missing_throws = false;
  try {
    with_header.index_of("nope");
  } catch (const lazycsv::error &) {
    missing_throws = true;
  }
  check(missing_throws, "index_of unknown column throws");

  // Rows and cells: a quoted cell keeps its delimiter, trimming strips
  // spaces and tabs, and the last line needs no newline.
  const rows_t expected = {
      {"alice", "hi, there"}, {"bob", "say \"\"x\"\""}, {"carol", "3"}};
  check(trimmed_rows(with_header) == expected, "rows with header");

  // Quoted cell with an exact value, raw and unescaped.
  auto it = with_header.begin();
  ++it;
  const auto quoted = (*it).cells(0, 1)[1];
  check(quoted.raw() == "say \"\"x\"\"", "quoted raw");
  check(quoted.unescaped() == "say \"x\"", "quoted unescaped");

  // Without a header every line is a row; CRLF line ends are dropped.
  lazycsv::parser<std::string, lazycsv::has_header<false>> no_header{
      "a,b\r\n1,2\r\n"};
  check(trimmed_rows(no_header) == (rows_t{{"a", "b"}, {"1", "2"}}),
        "rows without header, CRLF");

  // Custom delimiter and out-of-range cell.
  lazycsv::parser<std::string, lazycsv::has_header<false>,
                  lazycsv::delimiter<';'>>
      semi{"x;y;z"};
  check(trimmed_rows(semi) == (rows_t{{"x", "y", "z"}}), "semicolon delimiter");
  bool few_throws = false;
  try {
    (*semi.begin()).cells(5);
  } catch (const lazycsv::error &) {
    few_throws = true;
  }
  check(few_throws, "cells beyond the row throws");

  // The default source memory-maps a file.
  const std::string path =
      (std::filesystem::temp_directory_path() /
       ("lazycsv_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
        ".csv"))
          .string();
  {
    std::ofstream out(path, std::ios::binary);
    out << "id,word\n7,seven\n8,eight\n";
    check(out.good(), "fixture written");
  }
  {
    lazycsv::parser<> mapped{path};
    check(mapped.index_of("word") == 1, "mmap header");
    check(trimmed_rows(mapped) == (rows_t{{"7", "seven"}, {"8", "eight"}}),
          "mmap rows");
  }
  std::remove(path.c_str());

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
