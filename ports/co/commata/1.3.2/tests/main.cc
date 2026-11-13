// Parses CSV and TSV text into commata::stored_table (narrow and wide), scans
// typed fields with commata::table_scanner, pulls fields with
// commata::table_pull, extracts records with commata::record_extractor, and
// checks the positions that parse and translation errors report.
#include <commata/field_scanners.hpp>
#include <commata/parse_csv.hpp>
#include <commata/parse_error.hpp>
#include <commata/parse_tsv.hpp>
#include <commata/record_extractor.hpp>
#include <commata/stored_table.hpp>
#include <commata/table_pull.hpp>
#include <commata/table_scanner.hpp>
#include <commata/text_error.hpp>
#include <commata/text_value_translation.hpp>

#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void test_stored_table()
{
    commata::stored_table table;
    check(commata::parse_csv(std::istringstream("name,\"say \"\"hi\"\", then go\",n\n"
                                                "a,\"x,y\",1\n"
                                                "\n"
                                                "b,\"two\nlines\",2\r\n"),
                             commata::make_stored_table_builder(table)),
          "parse_csv reports completion");
    // The empty line is no record.
    check(table.size() == 3, "three records");
    check(table[0].size() == 3 && table[0][1] == "say \"hi\", then go", "escaped quotes and a quoted comma");
    check(table[1][0] == "a" && table[1][1] == "x,y" && table[1][2] == "1", "a quoted comma");
    check(table[2][1] == "two\nlines" && table[2][2] == "2", "a quoted newline and a CRLF record end");

    table[1][1].erase(1);
    check(table[1][1] == "x" && table[1][1].size() == 1, "values are editable in place");
}

void test_tsv_and_wide()
{
    commata::stored_table tsv;
    commata::parse_tsv(std::istringstream("a\tb\nc\t\"d,e\"\n"), commata::make_stored_table_builder(tsv));
    check(tsv.size() == 2 && tsv[1][0] == "c" && tsv[1][1] == "\"d,e\"", "TSV keeps quotes and commas literal");

    commata::wstored_table wide;
    commata::parse_csv(std::wistringstream(L"k,v\né,\"1,5\"\n"), commata::make_stored_table_builder(wide));
    check(wide.size() == 2 && wide[1][0] == L"é" && wide[1][1] == L"1,5", "wide-character CSV");
}

void test_parse_error()
{
    commata::stored_table table;
    bool threw = false;
    try {
        commata::parse_csv(std::istringstream("a,b\nc,d\"\n"), commata::make_stored_table_builder(table));
    } catch (const commata::parse_error &e) {
        threw = true;
        // Zero-based line 1, column 3; text_error_info prints them one-based.
        const auto position = e.get_physical_position();
        check(position && position->first == 1 && position->second == 3, "the error's position");
        check(commata::to_string(commata::text_error_info(e)).find("line 2 column 4") != std::string::npos,
              "text_error_info's one-based position");
    }
    check(threw, "a quote inside an unquoted value throws commata::parse_error");
}

void test_table_scanner()
{
    std::vector<std::string> names;
    std::vector<int> ages;
    commata::table_scanner scanner(1); // one header record
    scanner.set_field_scanner(0, commata::make_field_translator(names));
    scanner.set_field_scanner(2, commata::make_field_translator(ages));
    commata::parse_csv(std::istringstream("name,city,age\nAnn,Oslo,31\nBo,Lima,47\n"), std::move(scanner));
    check(names == std::vector<std::string>{"Ann", "Bo"} && ages == std::vector<int>{31, 47},
          "table_scanner translates the scanned fields");

    std::vector<int> bad;
    commata::table_scanner strict;
    strict.set_field_scanner(0, commata::make_field_translator(bad));
    bool threw = false;
    try {
        commata::parse_csv(std::istringstream("12\nx3\n"), std::move(strict));
    } catch (const commata::text_value_invalid_format &e) {
        threw = true;
        const auto position = e.get_physical_position();
        check(position && position->first == 1, "the translation error's line");
    }
    check(threw && bad == std::vector<int>{12}, "a non-numeric int field throws text_value_invalid_format");
}

void test_pull_and_extract()
{
    // Skip the header, then skip the first field of each record; the empty
    // line is ignored.
    auto pull = commata::make_table_pull(commata::make_csv_source(
        std::istringstream("Constellation,Name\nVirgo,\"Spica, alpha\"\nVirgo,Zavijava\n\nCygnus,Deneb\n")));
    std::vector<std::string> names;
    while (pull.skip_record()(1)) {
        names.emplace_back(*pull);
    }
    check(names == std::vector<std::string>{"Spica, alpha", "Zavijava", "Deneb"}, "table_pull visits the second fields");

    // A string literal is parsed in place rather than through a stream.
    std::stringbuf out;
    check(commata::parse_csv("k,v\na,1\n\"b\",2\nb,3\n", commata::make_record_extractor(out, "k", "b")),
          "record_extractor parse reports completion");
    check(out.str() == "k,v\n\"b\",2\nb,3\n", "record_extractor keeps the header and the matching records verbatim");
}

} // namespace

int main()
{
    test_stored_table();
    test_tsv_and_wide();
    test_parse_error();
    test_table_scanner();
    test_pull_and_extract();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("commata 1.3.2 ok");
    return 0;
}
