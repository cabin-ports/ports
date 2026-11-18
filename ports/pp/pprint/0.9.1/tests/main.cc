// pprint 0.9.1 defines non-inline functions, so the test stays in one translation unit.  Expected
// strings are pprint's exact output, including its ", " before each line break.
#include <pprint.hpp>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

std::string escaped(const std::string &text) {
    std::string out;
    for (char c : text)
        out += c == '\n' ? std::string("\\n") : std::string(1, c);
    return out;
}

template <class F> void expect(F print, const std::string &expected, int line) {
    std::stringstream ss;
    pprint::PrettyPrinter printer(ss);
    print(printer);
    if (ss.str() != expected) {
        std::fprintf(stderr, "main.cc:%d: got \"%s\", expected \"%s\"\n", line, escaped(ss.str()).c_str(),
                     escaped(expected).c_str());
        ++failures;
    }
}
#define EXPECT(statements, expected) expect([](pprint::PrettyPrinter &p) { statements; }, expected, __LINE__)

// The bundled magic_enum names enumerators.  An enum class also needs an operator<<, which pprint
// uses for values without a name, and on Apple clang 21 an enum without a fixed underlying type can
// fail to compile.
enum Level : int { LOW, MEDIUM, HIGH };
enum class Color { red = 1, green = 2 };
std::ostream &operator<<(std::ostream &os, Color c) { return os << "Color(" << static_cast<int>(c) << ")"; }

struct Point {
    int x, y;
};
std::ostream &operator<<(std::ostream &os, const Point &p) { return os << "Point(" << p.x << ", " << p.y << ")"; }

}  // namespace

int main() {
    EXPECT(p.print(5), "5\n");
    EXPECT(p.print(-3.5), "-3.5\n");
    EXPECT(p.print(2.5f), "2.5f\n");
    EXPECT(p.print(true), "true\n");
    EXPECT(p.print('x'), "'x'\n");
    EXPECT(p.print("Hello"), "\"Hello\"\n");
    EXPECT(p.print(std::string("str")), "\"str\"\n");
    EXPECT(p.print(nullptr), "nullptr\n");
    EXPECT(p.print(static_cast<int *>(nullptr)), "nullptr\n");
    EXPECT(p.quotes(false); p.print('A', 'B', 'C'), "A B C\n");
    EXPECT(p.line_terminator(";\n"); p.print(1); p.print("a"), "1;\n\"a\";\n");

    EXPECT(p.print(std::vector<int>{1, 2, 3}), "[\n  1, \n  2, \n  3\n]\n");
    EXPECT(p.print(std::vector<int>{}), "[]\n");
    EXPECT(p.print(std::vector<std::vector<int>>{{1, 2}, {3}}), "[\n  [1, 2], \n  [3]\n]\n");
    EXPECT(p.compact(true); p.print(std::vector<std::vector<int>>{{1, 2}, {3}}), "[[1, 2], [3]]\n");
    EXPECT(p.print(std::vector<std::vector<std::vector<int>>>{{{1}, {2, 3}}, {{4}}}),
           "[\n  [[1], [2, 3]], \n  [[4]]\n]\n");
    EXPECT(p.compact(true); p.print(std::vector<std::map<std::string, int>>{{{"a", 1}, {"b", 2}}, {{"c", 3}}}),
           "[{\"a\" : 1, \"b\" : 2}, {\"c\" : 3}]\n");
    EXPECT(p.print(std::map<std::string, int>{{"one", 1}}), "{\"one\" : 1}\n");
    EXPECT(p.print(std::map<std::string, int>{{"one", 1}, {"three", 3}, {"two", 2}}),
           "{\n  \"one\" : 1, \n  \"three\" : 3, \n  \"two\" : 2\n}\n");
    EXPECT(p.print(std::map<int, std::vector<char>>{{1, {'a', 'b'}}, {2, {'c'}}}),
           "{\n  1 : ['a', 'b'], \n  2 : ['c']\n}\n");
    EXPECT(p.indent(4); p.print(std::map<int, std::vector<char>>{{1, {'a', 'b'}}, {2, {'c'}}}),
           "{\n    1 : ['a', 'b'], \n    2 : ['c']\n}\n");
    EXPECT(p.print(std::set<int>{3, 1, 2}), "{\n  1, \n  2, \n  3\n}\n");
    EXPECT(p.print(std::deque<double>{0.5, 1.5}), "[\n  0.5, \n  1.5\n]\n");
    EXPECT(p.print(std::array<int, 3>{7, 8, 9}), "[\n  7, \n  8, \n  9\n]\n");
    EXPECT(std::queue<int> q; q.push(1); q.push(2); p.print(q), "[1, 2]\n");
    EXPECT(std::stack<int> s; s.push(1); s.push(2); p.print(s), "[\n  2, \n  1\n]\n");

    EXPECT(p.print(std::pair<int, std::string>{1, "one"}), "(1, \"one\")\n");
    EXPECT(p.print(std::make_tuple(1, std::string("two"), '3', "four")), "(1, \"two\", '3', \"four\")\n");
    EXPECT(p.print(std::optional<int>(7)), "7\n");
    EXPECT(p.print(std::optional<int>()), "nullopt\n");
    EXPECT(p.print(std::variant<int, std::string>(std::string("v"))), "\"v\"\n");
    EXPECT(p.print(std::variant<int, std::string>(4)), "4\n");
    EXPECT(p.print(std::complex<double>(1, -2)), "(1 + -2i)\n");
    EXPECT(p.print(Point{1, 2}), "Point(1, 2)\n");

    EXPECT(p.print(MEDIUM), "MEDIUM\n");
    EXPECT(p.print(static_cast<Level>(200)), "200\n");
    EXPECT(p.compact(true); p.print(std::vector<Level>{LOW, HIGH}), "[LOW, HIGH]\n");
    EXPECT(p.print(Color::green), "green\n");
    EXPECT(p.print(static_cast<Color>(200)), "Color(200)\n");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("pprint: ok\n");
    return 0;
}
