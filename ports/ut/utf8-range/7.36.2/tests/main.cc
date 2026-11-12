// Checks utf8_validity.h's absl::string_view API, and utf8_range's C API from
// C++.
#include <cstdio>
#include <string>

#include <utf8_validity.h>

#include "absl/strings/string_view.h"

int main() {
  int failures = 0;
  auto expect = [&](const char* what, bool ok) {
    if (!ok) {
      std::fprintf(stderr, "failed: %s\n", what);
      ++failures;
    }
  };

  expect("ASCII is valid", utf8_range::IsStructurallyValid("hello, world"));
  expect("embedded NUL counts toward the length",
         utf8_range::SpanStructurallyValid(absl::string_view("a\0b", 3)) == 3);
  expect("surrogate is invalid",
         !utf8_range::IsStructurallyValid("\xED\xA0\x80"));

  // 24 four-byte code points, long enough for the SIMD loop.
  std::string text;
  for (int i = 0; i < 24; ++i) text += "\xF0\x9F\x98\x80";
  expect("long text is valid", utf8_range::IsStructurallyValid(text));
  expect("long text's valid span is all of it",
         utf8_range::SpanStructurallyValid(text) == text.size());
  // Overwrite the 10th code point's last byte with ASCII.
  text[9 * 4 + 3] = '!';
  expect("damaged text is invalid", !utf8_range::IsStructurallyValid(text));
  expect("damaged text's valid span stops at the 10th code point",
         utf8_range::SpanStructurallyValid(text) == 9 * 4);
  expect("C API agrees",
         utf8_range_ValidPrefix(text.data(), text.size()) == 9 * 4 &&
             !utf8_range_IsValid(text.data(), text.size()));

  if (failures != 0) return 1;
  std::puts("utf8_validity C++ API OK");
  return 0;
}
