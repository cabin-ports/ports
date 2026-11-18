#include <uni_algo/case.h>
#include <uni_algo/conv.h>
#include <uni_algo/norm.h>
#include <uni_algo/prop.h>

#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

int main() {
  // Straße -> STRASSE: the full case mapping of U+00DF.
  check(una::cases::to_uppercase_utf8("Stra\xC3\x9F" "e") == "STRASSE",
        "uppercase of Strasse with sharp s");
  // ДВА -> два
  check(una::cases::to_lowercase_utf8("\xD0\x94\xD0\x92\xD0\x90") ==
            "\xD0\xB4\xD0\xB2\xD0\xB0",
        "lowercase of Cyrillic");

  // W + U+0302 composes to U+0174.
  check(una::norm::to_nfc_utf8("W\xCC\x82") == "\xC5\xB4", "NFC composes");
  check(una::norm::to_nfd_utf8("\xC5\xB4") == "W\xCC\x82", "NFD decomposes");
  check(!una::norm::is_nfc_utf8("W\xCC\x82"), "decomposed text is not NFC");
  check(una::norm::is_nfc_utf8("\xC5\xB4"), "composed text is NFC");

  // U+1F600 is the surrogate pair D83D DE00.
  const std::u16string pair = u"a\xD83D\xDE00z";
  const std::string utf8 = una::utf16to8(pair);
  check(utf8 == "a\xF0\x9F\x98\x80z", "utf16to8 joins a surrogate pair");
  check(una::utf8to16u(utf8) == pair, "utf8to16u restores the pair");

  check(una::codepoint::is_alphabetic(U'\x0416'), "U+0416 is alphabetic");
  check(una::codepoint::is_numeric(U'\x0663'), "U+0663 is numeric");
  check(!una::codepoint::is_alphabetic(U'7'), "7 is not alphabetic");

  if (failures != 0) {
    return 1;
  }
  std::printf("uni-algo 1.2.0 C++ ok\n");
  return 0;
}
