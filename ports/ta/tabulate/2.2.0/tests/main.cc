#include <tabulate/table.hpp>
#include <tabulate/tabulate.hpp>

#include <clocale>
#include <iostream>
#include <string>

using tabulate::FontAlign;
using tabulate::Table;
using Row_t = Table::Row_t;

static int failures = 0;

// A nested table is colorized like std::cout (table.hpp:104), so when stdout
// is a terminal its cell text carries style resets; compare without them.
static std::string without_escapes(std::string text) {
  for (std::string::size_type at; (at = text.find("\033[")) != std::string::npos;) {
    text.erase(at, text.find('m', at) + 1 - at);
  }
  return text;
}

static void expect(const char *what, Table &table, const std::string &expected) {
  const std::string actual = without_escapes(table.str());
  if (actual != expected) {
    std::cerr << "FAIL: " << what << "\nexpected:\n" << expected << "\nactual:\n" << actual << '\n';
    ++failures;
  }
}

// On Unix the width of a multi-byte cell comes from wcwidth() under the
// cell's LC_CTYPE locale, which must be a UTF-8 one; Windows ignores it.
static std::string utf8_locale() {
#ifdef _WIN32
  return "";
#else
  const char *names[] = {"C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8"};
  for (const char *name : names) {
    if (std::setlocale(LC_CTYPE, name) != nullptr) {
      std::setlocale(LC_CTYPE, "C");
      return name;
    }
  }
  std::cerr << "FAIL: no UTF-8 locale\n";
  ++failures;
  return "";
#endif
}

int main() {
  static_assert(TABULATE_VERSION_MAJOR == 2 && TABULATE_VERSION_MINOR == 2 && TABULATE_VERSION_PATCH == 0,
                "tabulate 2.2.0");

  {
    Table table;
    table.add_row(Row_t{"Item", "Qty"});
    table.add_row(Row_t{"apple", "3"});
    table.add_row(Row_t{"kiwi", "12"});
    table.column(1).format().font_align(FontAlign::right);
    expect("columns fit the widest cell; a right-aligned column", table,
           "+-------+-----+\n"
           "| Item  | Qty |\n"
           "+-------+-----+\n"
           "| apple |   3 |\n"
           "+-------+-----+\n"
           "| kiwi  |  12 |\n"
           "+-------+-----+");
  }

  {
    Table table;
    table.add_row(Row_t{"hi", "a long sentence"});
    table[0][0].format().width(8).font_align(FontAlign::center);
    table[0][1].format().width(10);
    expect("a fixed-width centered cell beside a word-wrapped one", table,
           "+--------+----------+\n"
           "|   hi   | a long   |\n"
           "|        | sentence |\n"
           "+--------+----------+");
  }

  {
    Table table;
    table.add_row(Row_t{"abcdefghij"});
    table[0][0].format().width(6);
    expect("a word longer than the cell is split with hyphens", table,
           "+------+\n"
           "| abc- |\n"
           "| def- |\n"
           "| ghij |\n"
           "+------+");
  }

  {
    Table table;
    table.add_row(Row_t{"a", "bc"});
    table.format().border_top("=").border_bottom("~").border_left("!").border_right("!").corner("#");
    expect("custom borders and corners", table,
           "#===#====#\n"
           "! a ! bc !\n"
           "#~~~#~~~~#");
  }

  {
    Table table;
    table.add_row(Row_t{"Word", "Lang"});
    table.add_row(Row_t{"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "ja"}); // 日本語, 9 bytes, 6 columns
    table.add_row(Row_t{"caf\xc3\xa9", "fr"});                         // café, 5 bytes, 4 columns
    table.column(0).format().multi_byte_characters(true).locale(utf8_locale());
    // Trimming would pass the UTF-8 bytes to std::isspace as negative chars.
    table.format().trim_mode(tabulate::Format::TrimMode::kNone);
    expect("multi-byte cells are measured in display columns", table,
           "+--------+------+\n"
           "| Word   | Lang |\n"
           "+--------+------+\n"
           "| \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e | ja   |\n"
           "+--------+------+\n"
           "| caf\xc3\xa9   | fr   |\n"
           "+--------+------+");
  }

  {
    // Upstream's test/nested_tables.cpp expectations.
    Table inner;
    inner.add_row(Row_t{"key", "value1\nvalue2"});
    inner[0][0].format().hide_border_left();
    expect("a hidden left border keeps the row width", inner,
           "+-----+--------+\n"
           "  key | value1 |\n"
           "      | value2 |\n"
           "+-----+--------+");

    Table outer;
    outer.add_row(Row_t{inner});
    expect("a nested table renders inside a cell", outer,
           "+------------------+\n"
           "| +-----+--------+ |\n"
           "|   key | value1 | |\n"
           "|       | value2 | |\n"
           "| +-----+--------+ |\n"
           "+------------------+");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "tabulate 2.2.0 C++ ok\n";
  return 0;
}
