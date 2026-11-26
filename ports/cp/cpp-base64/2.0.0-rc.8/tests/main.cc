#include <base64.h>

#include <cstdio>
#include <stdexcept>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  // RFC 4648 section 10.
  const char* const plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char* const coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  for (int i = 0; i < 7; ++i) {
    check(base64_encode(std::string(plain[i])) == coded[i], "RFC 4648 encode");
    check(base64_decode(std::string(coded[i])) == plain[i], "RFC 4648 decode");
  }

  // Unpadded input (rc.08) and the '.' padding of the URL-safe form.
  check(base64_decode(std::string("Zg")) == "f", "unpadded, 1 byte");
  check(base64_decode(std::string("Zm8")) == "fo", "unpadded, 2 bytes");
  check(base64_decode(std::string("Zm9vYmE")) == "fooba", "unpadded, multi-chunk");
  check(base64_decode(std::string("Zg..")) == "f", "dot padding");

  // Bytes whose sextets are 62 and 63.
  const std::string sym("\xfb\xff\xbf", 3);
  check(base64_encode(sym) == "+/+/", "standard alphabet");
  check(base64_encode(sym, true) == "-_-_", "URL-safe alphabet");
  check(base64_decode(std::string("-_-_")) == sym, "URL-safe decode");
  const std::string two("\xfb\xff", 2);
  check(base64_encode(two) == "+/8=", "standard padding");
  check(base64_encode(two, true) == "-_8.", "URL-safe padding");
  const unsigned char raw[] = {0xfb, 0xff, 0xbf};
  check(base64_encode(raw, sizeof raw) == "+/+/", "pointer and length overload");

  // Line breaking: 64 columns for PEM, 76 for MIME, no trailing newline.
  const std::string a48(48, 'A'), a49(49, 'A'), a57(57, 'A'), a58(58, 'A');
  std::string pem_row, mime_row;
  for (int i = 0; i < 16; ++i) pem_row += "QUFB";
  for (int i = 0; i < 19; ++i) mime_row += "QUFB";
  check(base64_encode_pem(a48) == pem_row, "PEM exactly one line");
  check(base64_encode_pem(a49) == pem_row + "\nQQ==", "PEM wraps at 64");
  check(base64_encode_mime(a57) == mime_row, "MIME exactly one line");
  check(base64_encode_mime(a58) == mime_row + "\nQQ==", "MIME wraps at 76");
  check(base64_decode(pem_row + "\nQQ==", true) == a49, "decode with line breaks removed");

  // Invalid input.
  bool threw = false;
  try {
    base64_decode(std::string("Zm9v\nYmFy"));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "line break rejected unless removed");

  return failures == 0 ? 0 : 1;
}
