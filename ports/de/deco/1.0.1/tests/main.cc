#include <deco/Deco.h>
#include <deco/NVP.h>
#include <deco/list.h>
#include <deco/types/arithmetic.h>
#include <deco/types/vector.h>

#include <gs/serializer.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

struct A {
  int i = 2;
  std::string s = "an entry";
};

namespace deco {
template <typename Stream>
void serialize(Stream& stream, A& value) {
  gs::serializer(stream, begin_list("a"), make_NVP("i", value.i),
                 make_NVP("s", value.s), end_list);
}
}  // namespace deco

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static std::string_view text(const deco::EntryObject& e) {
  return e.content;
}

int main() {
  // Parser: nested lists, an anonymous list, content escaped with '.
  const std::string doc =
      "hello\n"
      "world!:\n"
      "\tfrom\n"
      "\tDeco:\n"
      "\t\tFormat\n"
      "\t:\n"
      ":\n"
      "ends with colon:'\n"
      "'\t starts with whitespace\n"
      "':\n"
      "\tanonymous\n"
      ":\n";
  const auto objects = deco::parse(doc.begin(), doc.end());
  check(objects.size() == 5, "top-level count");
  if (objects.size() == 5) {
    check(text(objects[0]) == "hello" && objects[0].entries.empty(), "entry");
    check(text(objects[1]) == "world!" && objects[1].entries.size() == 2,
          "list");
    if (objects[1].entries.size() == 2) {
      check(text(objects[1].entries[0]) == "from", "nested entry");
      const auto& deco_list = objects[1].entries[1];
      check(text(deco_list) == "Deco" && deco_list.entries.size() == 1 &&
                text(deco_list.entries[0]) == "Format",
            "nested list");
    }
    check(text(objects[2]) == "ends with colon:", "escaped trailing colon");
    check(text(objects[3]) == "\t starts with whitespace",
          "escaped leading whitespace");
    check(text(objects[4]).empty() && objects[4].entries.size() == 1 &&
              text(objects[4].entries[0]) == "anonymous",
          "anonymous list");
  }

  // Strings, integers (Spirit X3 int_parser) and floats (fmt, X3
  // real_parser) round-trip through lists.
  {
    const std::vector<std::string> s{"1", "2", "3"};
    deco::OutputStream_indent out;
    deco::serialize(out, deco::make_list("v", s));
    check(out.str == "v:\n\t1\n\t2\n\t3\n:\n", "string list text");
    auto in = deco::make_InputStream(out.str.cbegin());
    std::vector<std::string> back;
    deco::serialize(in, deco::make_list("v", back));
    check(back == s, "string list round trip");
  }
  {
    const std::vector<int> n{1, -2, 30};
    deco::OutputStream out;
    deco::serialize(out, deco::make_list("n", n));
    check(out.str == "n:\n1\n-2\n30\n:\n", "int list text");
    auto in = deco::make_InputStream(out.str.cbegin());
    std::vector<int> back;
    deco::serialize(in, deco::make_list("n", back));
    check(back == n, "int list round trip");
  }
  {
    const std::vector<double> d{0.5, 2.25, -123.125};
    deco::OutputStream out;
    deco::serialize(out, deco::make_list("d", d));
    check(out.str == "d:\n0.5\n2.25\n-123.125\n:\n", "double list text");
    auto in = deco::make_InputStream(out.str.cbegin());
    std::vector<double> back;
    deco::serialize(in, deco::make_list("d", back));
    check(back == d, "double list round trip");
  }

  // A struct through generic_serialization, with escaped NVP content.
  {
    A a;
    a.i = -7;
    a.s = "tricky:";
    deco::OutputStream_indent out;
    gs::serializer(out, a);
    check(out.str == "a:\n\ti: -7\n\ts: tricky:'\n:\n", "struct text");
    A back;
    auto in = deco::make_InputStream(out.str.cbegin());
    gs::serializer(in, back);
    check(back.i == -7 && back.s == "tricky:", "struct round trip");
  }

  return failures == 0 ? 0 : 1;
}
