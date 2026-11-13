// Expected values follow README.md: read_header matches columns by name in any order, columns
// are trimmed and then unquoted, and comments and overflow behave as the policies document.  The
// exception classes and their fields, and the colN names, come from csv.h.
#include <csv.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

void testHeaderAndTypes() {
    const std::string data =
        "name, age ,city,height\n"
        "alice, 30 ,Paris,1.75\n"
        "bob,41,Rome,-2.5e1\n";
    io::CSVReader<3> in("people.csv", data.data(), data.data() + data.size());
    in.read_header(io::ignore_extra_column, "age", "name", "height");
    CHECK(in.has_column("age"));
    int age = 0;
    std::string name;
    double height = 0;
    CHECK(in.read_row(age, name, height));
    CHECK(age == 30 && name == "alice" && std::fabs(height - 1.75) < 1e-12);
    CHECK(in.read_row(age, name, height));
    CHECK(age == 41 && name == "bob" && std::fabs(height + 25.0) < 1e-12);
    CHECK(!in.read_row(age, name, height));
    CHECK(in.get_file_line() == 3);
}

void testQuotesAndComments() {
    const std::string data =
        "# exported\n"
        "id;text\n"
        "1;\"semi;colon\"\n"
        "2;\"say \"\"hi\"\"\"\n"
        "# trailer\n"
        "3; padded \n"
        "4;\" kept \"\n";
    io::CSVReader<2, io::trim_chars<' '>, io::double_quote_escape<';', '"'>, io::throw_on_overflow,
                  io::single_line_comment<'#'>>
        in("notes.csv", data.data(), data.data() + data.size());
    in.read_header(io::ignore_no_column, "id", "text");
    unsigned id = 0;
    std::string text;
    CHECK(in.read_row(id, text) && id == 1 && text == "semi;colon");
    CHECK(in.read_row(id, text) && id == 2 && text == "say \"hi\"");
    CHECK(in.read_row(id, text) && id == 3 && text == "padded");
    // Trimming happens before unquoting, so spaces inside quotes stay.
    CHECK(in.read_row(id, text) && id == 4 && text == " kept ");
    CHECK(in.get_file_line() == 7);
    CHECK(!in.read_row(id, text));
}

void testOverflowPolicies() {
    const std::string data = "300,-200\n";
    {
        io::CSVReader<2, io::trim_chars<>, io::no_quote_escape<','>, io::set_to_max_on_overflow> in(
            "limits.csv", data.data(), data.data() + data.size());
        unsigned char u = 0;
        signed char s = 0;
        CHECK(in.read_row(u, s) && u == 255 && s == -128);
    }
    io::CSVReader<2> in("limits.csv", data.data(), data.data() + data.size());
    unsigned char u = 0;
    signed char s = 0;
    bool thrown = false;
    try {
        in.read_row(u, s);
    } catch (const io::error::integer_overflow &e) {
        // Without a header the columns are named col1, col2, ...
        thrown = std::strcmp(e.column_name, "col1") == 0 && std::strcmp(e.column_content, "300") == 0 &&
                 e.file_line == 1 && std::strcmp(e.file_name, "limits.csv") == 0;
    }
    CHECK(thrown);
}

void testErrors() {
    {
        const std::string data = "a,b\n1,2,3\n";
        io::CSVReader<2> in("wide.csv", data.data(), data.data() + data.size());
        in.read_header(io::ignore_no_column, "a", "b");
        int a = 0, b = 0;
        bool thrown = false;
        try {
            in.read_row(a, b);
        } catch (const io::error::too_many_columns &e) {
            thrown = e.file_line == 2 && std::strcmp(e.file_name, "wide.csv") == 0;
        }
        CHECK(thrown);
    }
    {
        const std::string data = "a\n5\n";
        io::CSVReader<2> strict("narrow.csv", data.data(), data.data() + data.size());
        bool thrown = false;
        try {
            strict.read_header(io::ignore_no_column, "a", "b");
        } catch (const io::error::missing_column_in_header &e) {
            thrown = std::strcmp(e.column_name, "b") == 0;
        }
        CHECK(thrown);

        // ignore_missing_column leaves the missing column's variable untouched.
        io::CSVReader<2> lenient("narrow.csv", data.data(), data.data() + data.size());
        lenient.read_header(io::ignore_missing_column, "a", "b");
        CHECK(lenient.has_column("a") && !lenient.has_column("b"));
        int a = 0, b = 42;
        CHECK(lenient.read_row(a, b) && a == 5 && b == 42);
    }
    {
        const std::string data = "n\n12x\n";
        io::CSVReader<1> in("digits.csv", data.data(), data.data() + data.size());
        in.read_header(io::ignore_no_column, "n");
        int n = 0;
        bool thrown = false;
        try {
            in.read_row(n);
        } catch (const io::error::no_digit &e) {
            thrown = std::strcmp(e.column_name, "n") == 0 && std::strcmp(e.column_content, "12x") == 0;
        }
        CHECK(thrown);
    }
    {
        const std::string data = "c\nx\nxy\n";
        io::CSVReader<1> in("chars.csv", data.data(), data.data() + data.size());
        in.read_header(io::ignore_no_column, "c");
        char c = 0;
        CHECK(in.read_row(c) && c == 'x');
        bool thrown = false;
        try {
            in.read_row(c);
        } catch (const io::error::invalid_single_character &) {
            thrown = true;
        }
        CHECK(thrown);
    }
    {
        const std::string data = "a,a\n";
        io::CSVReader<1> in("dup.csv", data.data(), data.data() + data.size());
        bool thrown = false;
        try {
            in.read_header(io::ignore_extra_column, "a");
        } catch (const io::error::duplicated_column_in_header &e) {
            thrown = std::strcmp(e.column_name, "a") == 0;
        }
        CHECK(thrown);
    }
    {
        const std::string data;
        io::CSVReader<1> in("empty.csv", data.data(), data.data());
        bool thrown = false;
        try {
            in.read_header(io::ignore_no_column, "a");
        } catch (const io::error::header_missing &e) {
            thrown = std::strcmp(e.file_name, "empty.csv") == 0;
        }
        CHECK(thrown);
    }
    {
        bool thrown = false;
        try {
            io::LineReader in("no-such-dir/no-such-file.csv");
        } catch (const io::error::can_not_open_file &e) {
            thrown = e.errno_value != 0 && std::strcmp(e.file_name, "no-such-dir/no-such-file.csv") == 0;
        }
        CHECK(thrown);
    }
}

void testLineReader() {
    // A UTF-8 BOM is skipped, \r\n is handled, and a missing final newline is fine.
    const std::string data = "\xEF\xBB\xBFone\r\ntwo\r\n\r\nthree";
    io::LineReader in("lines.txt", data.data(), data.data() + data.size());
    const char *expected[] = {"one", "two", "", "three"};
    for (const char *want : expected) {
        const char *line = in.next_line();
        CHECK(line != nullptr && std::strcmp(line, want) == 0);
    }
    CHECK(in.next_line() == nullptr);
    CHECK(in.get_file_line() == 4);
}

template <class Reader>
void sumRows(Reader &in, long long &rows, long long &first, long long &second) {
    long long a = 0, b = 0;
    rows = first = second = 0;
    while (in.read_row(a, b)) {
        ++rows;
        first += a;
        second += b;
    }
}

// LineReader reads the first 2 MiB itself and the rest on its worker thread.
void testLargeInput() {
    const long long count = 400000;
    std::string data;
    long long want_first = 0, want_second = 0;
    for (long long i = 0; i < count; ++i) {
        data += std::to_string(i) + ',' + std::to_string(i % 7) + '\n';
        want_first += i;
        want_second += i % 7;
    }
    CHECK(data.size() > (2u << 20));

    long long rows = 0, first = 0, second = 0;
    io::CSVReader<2> memory("big.csv", data.data(), data.data() + data.size());
    sumRows(memory, rows, first, second);
    CHECK(rows == count && first == want_first && second == want_second);

    std::istringstream stream(data);
    io::CSVReader<2> fromStream("big.csv", stream);
    sumRows(fromStream, rows, first, second);
    CHECK(rows == count && first == want_first && second == want_second);
}

}  // namespace

int main() {
    testHeaderAndTypes();
    testQuotesAndComments();
    testOverflowPolicies();
    testErrors();
    testLineReader();
    testLargeInput();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("fast-cpp-csv-parser: ok\n");
    return 0;
}
