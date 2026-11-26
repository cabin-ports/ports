#include <sjson/parser.h>
#include <sjson/writer.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAILED line %d: %s\n", __LINE__, #cond);            \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

class StringWriter final : public sjson::StreamWriter {
public:
  void write(const void* buffer, size_t size) override {
    out.append(static_cast<const char*>(buffer), size);
  }
  std::string out;
};
} // namespace

int main() {
  const char* text =
      "answer = 42.5\n"
      "flag = true\n"
      "name = \"cabin\"\n"
      "list = [ 1.5, 2.5, 3.5 ]\n"
      "obj = { inner = 7 }\n";

  // Parser: every value kind, then end of input.
  {
    sjson::Parser parser(text, std::strlen(text));
    double answer = 0;
    bool flag = false;
    sjson::StringView name;
    double list[3] = {};
    int32_t inner = 0;

    CHECK(parser.read("answer", answer));
    CHECK(answer == 42.5);
    CHECK(parser.read("flag", flag));
    CHECK(flag);
    CHECK(parser.read("name", name));
    CHECK(name.size() == 5 && name == "cabin");
    CHECK(parser.read("list", list, 3));
    CHECK(list[0] == 1.5 && list[1] == 2.5 && list[2] == 3.5);
    CHECK(parser.object_begins("obj"));
    CHECK(parser.read("inner", inner));
    CHECK(inner == 7);
    CHECK(parser.object_ends());
    CHECK(parser.remainder_is_comments_and_whitespace());
    CHECK(parser.eof());
    CHECK(parser.is_valid());
  }

  // Parser: a missing key fails and records an error.
  {
    sjson::Parser parser(text, std::strlen(text));
    double value = 0;
    CHECK(!parser.read("nokey", value));
    CHECK(!parser.is_valid());
    CHECK(parser.get_error().error == sjson::ParserError::IncorrectKey);
  }

  // Parser: content left after the last value is reported.
  {
    const char* extra = "key = 1\nstray";
    sjson::Parser parser(extra, std::strlen(extra));
    int32_t value = 0;
    CHECK(parser.read("key", value));
    CHECK(!parser.remainder_is_comments_and_whitespace());
    CHECK(!parser.eof());
    CHECK(!parser.is_valid());
  }

  // Writer: upstream tests/sources/test_writer.cpp pins these formats
  // (CRLF terminators, tab indentation, "[ a, b ]" arrays).
  {
    StringWriter sw;
    sjson::Writer writer(sw);
    writer.insert("answer", 42.5);
    writer.insert("flag", true);
    writer.insert("name", "cabin");
    writer.insert("list", [](sjson::ArrayWriter& array) {
      array.push(1.5);
      array.push(2.5);
      array.push(3.5);
    });
    writer.insert("obj", [](sjson::ObjectWriter& object) {
      object.insert("inner", int32_t(7));
    });
    CHECK(sw.out ==
          "answer = 42.5\r\n"
          "flag = true\r\n"
          "name = \"cabin\"\r\n"
          "list = [ 1.5, 2.5, 3.5 ]\r\n"
          "obj = {\r\n\tinner = 7\r\n}\r\n");

    // The writer's output parses back to the same values.
    sjson::Parser parser(sw.out.c_str(), sw.out.size());
    double answer = 0;
    CHECK(parser.read("answer", answer));
    CHECK(answer == 42.5);
  }

  return failures == 0 ? 0 : 1;
}
