#include <cpp-kana2/core/Convert.h>

#include <cstdio>
#include <string>

using namespace cpp_kana::v2;

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s (line %d)\n", #cond, __LINE__);             \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  CHECK(std::string(version()) == "2.0.0");

  // Sokuon doubles the next consonant; the long vowel mark repeats the vowel.
  auto kitte = kanaToRomaji("きって");
  CHECK(kitte.ok() && kitte.value().rendered() == "kitte");
  CHECK(kitte.value().moras().size() == 3);
  CHECK(kitte.value().moras()[1].kind == MoraKind::Sokuon);
  auto coffee = kanaToRomaji("コーヒー");
  CHECK(coffee.ok() && coffee.value().rendered() == "koohii");

  RomajiOptions dashed;
  dashed.longVowel = LongVowelPolicy::KatakanaDash;
  CHECK(kanaToRomaji("コーヒー", dashed).value().rendered() == "ko-hi-");

  // And back.
  auto hello = romajiToKana("konnichiha");
  CHECK(hello.ok() && hello.value().rendered() == "こんにちは");
  KanaOptions kata;
  kata.script = Script::Katakana;
  CHECK(romajiToKana("kitte", kata).value().rendered() == "キッテ");

  // Script conversion, half-width katakana included.
  CHECK(convertScript("ｶﾀｶﾅ", Script::Hiragana).value().rendered() == "かたかな");

  // A truncated UTF-8 sequence is a value, not an exception.
  auto bad = kanaToRomaji("\xE3\x81");
  CHECK(!bad.ok());
  CHECK(bad.error().code == Code::InvalidUtf8);
  CHECK(std::string(codeText(Code::InvalidUtf8)) == "invalid UTF-8");

  if (failures != 0) return 1;
  std::puts("ok");
  return 0;
}
