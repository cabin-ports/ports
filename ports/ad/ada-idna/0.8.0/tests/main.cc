// Consumer test for cabin-ports/ada-idna: UTS #46 conversion and Punycode.
#include <idna.h>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  using namespace ada::idna;

  check(to_ascii("me\xC3\x9F" "agefactory.ca") == "xn--meagefactory-m9a.ca",
        "to_ascii sharp s");
  check(to_ascii("b\xC3\xBC" "cher.de") == "xn--bcher-kva.de", "to_ascii umlaut");
  check(to_ascii("\xF0\x9F\x92\xA9.la") == "xn--ls8h.la", "to_ascii astral");
  check(to_ascii("EXAMPLE.COM") == "example.com", "to_ascii lowercases");
  check(to_ascii("B\xC3\x9C" "CHER.de") == "xn--bcher-kva.de", "to_ascii maps case");
  // NFC: "u" + combining diaeresis normalizes to the precomposed form.
  check(to_ascii("bu\xCC\x88" "cher.de") == "xn--bcher-kva.de", "to_ascii normalizes");

  std::string out;
  check(to_ascii("b\xC3\xBC" "cher.de", out) && out == "xn--bcher-kva.de", "to_ascii out");

  check(to_unicode("xn--bcher-kva.de") == "b\xC3\xBC" "cher.de", "to_unicode");
  check(to_unicode("xn--ls8h.la") == "\xF0\x9F\x92\xA9.la", "to_unicode astral");
  check(to_unicode("example.com") == "example.com", "to_unicode ascii");

  std::u32string u;
  check(punycode_to_utf32("bcher-kva", u) && u == U"bücher", "punycode_to_utf32");
  std::string p;
  check(utf32_to_punycode(U"bücher", p) && p == "bcher-kva", "utf32_to_punycode");
  check(verify_punycode("bcher-kva"), "verify_punycode");

  check(contains_forbidden_domain_code_point("exa mple.com"), "forbidden space");
  check(!contains_forbidden_domain_code_point("example.com"), "allowed");

  return failures == 0 ? 0 : 1;
}
