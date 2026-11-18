#include <tinyutf8/tinyutf8.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

static std::string bytes(const tiny_utf8::string& s) { return std::string(s.c_str(), s.size()); }

// length() counts code points, size() counts bytes, and at() takes a code point index.
static void test_indexing() {
  tiny_utf8::string s("H\xC3\xA9llo");
  CHECK(s.length() == 5);
  CHECK(s.size() == 6);
  CHECK(s.at(1) == U'é');
  CHECK(s.at(2) == U'l');
  CHECK(s.raw_at(3) == U'l');
  CHECK(s.find(U'l') == 2);
  CHECK(s.requires_unicode());

  s.push_back(U'\U0001F30D');
  CHECK(s.length() == 6);
  CHECK(s.size() == 10);
  CHECK(s.at(5) == U'\U0001F30D');
  CHECK(bytes(s) == "H\xC3\xA9llo\xF0\x9F\x8C\x8D");
  const char32_t prefix[] = U"H\u00E9l";
  const char32_t other[] = U"Hx";
  CHECK(s.starts_with(prefix));
  CHECK(!s.starts_with(other));

  // The const overload checks the index; the non-const one returns a lazy reference.
  const tiny_utf8::string& cs = s;
  bool threw = false;
  try {
    (void)cs.at(6);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);
}

static void test_insert_erase() {
  tiny_utf8::string s("H\xC3\xA9llo");
  s.insert(1, U'€');
  CHECK(s.length() == 6);
  CHECK(s.size() == 9);
  CHECK(bytes(s) == "H\xE2\x82\xAC\xC3\xA9llo");
  CHECK(s.at(2) == U'é');

  s.insert(5, tiny_utf8::string("\xC3\xBC\xF0\x9F\x8C\x8D"));  // before the o
  CHECK(s.length() == 8);
  CHECK(bytes(s) == "H\xE2\x82\xAC\xC3\xA9ll\xC3\xBC\xF0\x9F\x8C\x8Do");

  s.erase(2);  // the e-acute
  CHECK(s.length() == 7);
  CHECK(bytes(s) == "H\xE2\x82\xACll\xC3\xBC\xF0\x9F\x8C\x8Do");

  s.erase(0, 2);  // 'H' and the euro sign: four bytes
  CHECK(s.length() == 5);
  CHECK(s.size() == 9);
  CHECK(bytes(s) == "ll\xC3\xBC\xF0\x9F\x8C\x8Do");
  CHECK(s.at(3) == U'\U0001F30D');
}

// Strings of up to sizeof(string) - 1 bytes live inside the object.
static void test_sso() {
  const std::size_t cap = sizeof(tiny_utf8::string) - 1;
  tiny_utf8::string s(std::string(cap, 'a').c_str());
  CHECK(s.size() == cap);
  CHECK(s.sso_active());
  CHECK(!s.requires_unicode());

  s.push_back(U'é');  // two more bytes: moves to the heap
  CHECK(!s.sso_active());
  CHECK(s.length() == cap + 1);
  CHECK(s.size() == cap + 2);
  CHECK(s.at(cap) == U'é');
  CHECK(s.requires_unicode());

  s.erase(cap);  // back within the inline capacity
  CHECK(s.sso_active());
  CHECK(s.length() == cap);
  CHECK(bytes(s) == std::string(cap, 'a'));

  tiny_utf8::string wide;
  for (int i = 0; i < 100; ++i)
    wide.push_back(i % 2 ? U'é' : U'€');
  CHECK(!wide.sso_active());
  CHECK(wide.length() == 100);
  CHECK(wide.size() == 250);
  CHECK(wide.at(57) == U'é');
  CHECK(wide.at(98) == U'€');
}

// Invalid UTF-8 is not rejected: a byte that cannot start a complete sequence becomes its own code point, and a
// lead byte takes the following bytes without checking them.
static void test_invalid() {
  tiny_utf8::string lone("a\x80" "b");
  CHECK(lone.length() == 3);
  CHECK(lone.at(1) == 0x80);

  tiny_utf8::string truncated("ab\xC3");
  CHECK(truncated.length() == 3);
  CHECK(truncated.at(2) == 0xC3);

  tiny_utf8::string cut("\xE2\x82");
  CHECK(cut.length() == 2);
  CHECK(cut.at(0) == 0xE2 && cut.at(1) == 0x82);

  tiny_utf8::string unchecked("\xC3" "A");
  CHECK(unchecked.length() == 1);
  CHECK(unchecked.size() == 2);
  CHECK(unchecked.at(0) == 0xC1);
}

int main() {
  test_indexing();
  test_insert_erase();
  test_sso();
  test_invalid();
  std::puts("tiny-utf8 4.4.3 ok");
  return 0;
}
