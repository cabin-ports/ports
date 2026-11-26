#include <base64.h>

#include <cstdio>
#include <string>
#include <string_view>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// MSVC reports __cplusplus as 199711L, so upstream's guards hide the overloads there.
int main() {
#if __cplusplus >= 201703L
  std::string pem_row, mime_row;
  for (int i = 0; i < 16; ++i) pem_row += "QUFB";
  for (int i = 0; i < 19; ++i) mime_row += "QUFB";

  check(base64_encode(std::string_view("foobar")) == "Zm9vYmFy", "string_view encode");
  check(base64_encode(std::string_view("\xfb\xff\xbf", 3), true) == "-_-_", "string_view URL-safe");
  check(base64_decode(std::string_view("Zm9vYg")) == "foob", "string_view decode");
  check(base64_decode(std::string_view(pem_row + "\nQQ=="), true) == std::string(49, 'A'),
        "string_view decode without line breaks");
  check(base64_encode_pem(std::string_view(std::string(49, 'A'))) == pem_row + "\nQQ==",
        "string_view PEM");
  check(base64_encode_mime(std::string_view(std::string(58, 'A'))) == mime_row + "\nQQ==",
        "string_view MIME");
#endif
  return failures == 0 ? 0 : 1;
}
