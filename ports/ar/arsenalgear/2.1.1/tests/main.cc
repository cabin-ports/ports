// Exercises the string, math, container, type-trait and stream helpers
// against their documented results.  Terminal-only ANSI output is not run.
#include <arsenalgear/constants.hpp>
#include <arsenalgear/containers.hpp>
#include <arsenalgear/math.hpp>
#include <arsenalgear/operators.hpp>
#include <arsenalgear/stream.hpp>
#include <arsenalgear/system.hpp>
#include <arsenalgear/type.hpp>
#include <arsenalgear/utils.hpp>

#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "arsenalgear test failed: %s\n", #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

struct NotStreamable {};

int main() {
    // utils.cpp
    CHECK(agr::multi("ab", 3) == "ababab");
    CHECK(agr::multi("ab", 0).empty());
    const std::vector<std::string> parts = agr::split_string("a, b,c", ",\\s*");
    CHECK((parts == std::vector<std::string>{"a", "b", "c"}));
    CHECK((agr::split_string("a,,b", ",") == std::vector<std::string>{"a", "", "b"}));

    // operators.hpp
    CHECK(std::string("xy") * 2u == "xyxy");
#if !_MSC_VER
    CHECK(3u * std::string("z") == "zzz");
#endif

    // math.hpp
    CHECK(std::fabs(agr::roundoff(3.14159, 2) - 3.14) < 1e-9);
    CHECK(std::fabs(agr::roundoff(2.5, 0) - 3.0) < 1e-9);
    CHECK(agr::IsInBounds(5, 0, 10));
    CHECK(agr::IsInBounds(0, 0, 10));
    CHECK(!agr::IsInBounds(10, 0, 10));
    CHECK(!agr::IsInBounds(-1, 0, 10));

    // containers.hpp
    const std::map<int, std::string> m{{1, "a"}, {2, "b"}, {3, "c"}};
    CHECK((agr::extract_map_keys(m) == std::vector<int>{1, 2, 3}));
    CHECK((agr::extract_map_elem(m) == std::vector<std::string>{"a", "b", "c"}));

    // constants.hpp
    CHECK(agr::null_str<std::string>.empty());
    CHECK(agr::empty_space<std::string> == " ");

    // utils.hpp
    CHECK(agr::isFloatingPoint(1.5));
    CHECK(!agr::isFloatingPoint(1));
    CHECK(agr::one(7) == 1);
    CHECK(agr::one(2.5) == 0.0);

    const std::runtime_error err = agr::except_error_func("bad", "value", "here");
    CHECK(std::string(err.what()) ==
          "\033[31mbad \"\033[1mvalue\033[22m\" here\033[39m");

    CHECK(agr::StringConverter<char>("abc") == "abc");
    CHECK(agr::StringConverter<wchar_t>("abc") == L"abc");
    CHECK(agr::StringConverter<wchar_t>("\xc3\xa9") == std::wstring(1, wchar_t(0xe9)));
#ifndef __APPLE__
    CHECK(agr::StringConverter<char16_t>("\xc3\xa9") == std::u16string(1, char16_t(0xe9)));
    CHECK(agr::StringConverter<char32_t>("\xc3\xa9") == std::u32string(1, char32_t(0xe9)));
#endif

    CHECK(agr::is_escape("\033[1m", agr::ANSI::first));
    CHECK(!agr::is_escape("a\033[1m", agr::ANSI::first));
    CHECK(agr::is_escape("ab\033c", agr::ANSI::generic));
    CHECK(!agr::is_escape("abc", agr::ANSI::generic));
    CHECK(!agr::is_escape(5, agr::ANSI::generic));

    // type.hpp
    CHECK((agr::is_streamable<std::ostream, int>::value));
    CHECK(!(agr::is_streamable<std::ostream, NotStreamable>::value));
    CHECK(agr::is_str("abc"));
    CHECK(agr::is_str(std::string("abc")));
    CHECK(agr::is_str(std::string_view("abc")));
    CHECK(!agr::is_str(5));
    CHECK((agr::is_any<int, char, int>()));
    CHECK(!(agr::is_any<int, char, long>()));

    // stream.hpp: savebuf records what it forwards.
    std::ostringstream sink;
    agr::savebuf buf(sink.rdbuf());
    std::ostream out(&buf);
    out << "hello " << 42 << std::flush;
    CHECK(buf.str() == "hello 42");
    CHECK(sink.str() == "hello 42");
    CHECK(&agr::select_cout<char>::cout == &std::cout);
    CHECK(&agr::select_cout<wchar_t>::cout == &std::wcout);
    CHECK(&agr::select_cin<char>::cin == &std::cin);
    CHECK(&agr::select_cin<wchar_t>::cin == &std::wcin);

    // system.cpp
    std::string echoed = agr::getCommandOut("echo cabin");
    while (!echoed.empty() && (echoed.back() == '\n' || echoed.back() == '\r')) echoed.pop_back();
    CHECK(echoed == "cabin");

    return failures == 0 ? 0 : 1;
}
