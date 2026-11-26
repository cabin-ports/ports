#include <zeus/expected.hpp>

#include <cstdio>
#include <string>

using E = zeus::expected<int, std::string>;

static int failures = 0;
#define CHECK(c) \
  do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static E half(int x) {
  if (x % 2 != 0) return zeus::unexpected<std::string>("odd");
  return x / 2;
}

int main() {
  CHECK(half(84).has_value() && *half(84) == 42);
  CHECK(half(3).error() == "odd");
  CHECK(half(3).value_or(-1) == -1);

  CHECK(half(84).and_then(half).value() == 21);
  CHECK(half(84).and_then(half).and_then(half).error() == "odd");
  CHECK(half(84).transform([](int v) { return v + 1; }).value() == 43);
  CHECK(half(3).transform_error([](const std::string &e) { return e.size(); }).error() == 3u);
  CHECK(half(3).or_else([](const std::string &) { return E(0); }).value() == 0);
  CHECK(half(84).error_or("none") == "none");

  bool thrown = false;
  try {
    (void) half(3).value();
  } catch (const zeus::bad_expected_access<std::string> &e) {
    thrown = e.error() == "odd";
  }
  CHECK(thrown);

  CHECK((zeus::expected<void, int>{}.has_value()));
  CHECK((zeus::expected<void, int>(zeus::unexpect, 7).error() == 7));

  CHECK((zeus::expected<int, int>(3) == 3));
  CHECK((zeus::expected<int, int>(3) != 4));
  CHECK(zeus::unexpected<int>(4) == zeus::unexpected<int>(4));
  CHECK(zeus::unexpected<int>(4) != zeus::unexpected<int>(5));

  CHECK(ZEUS_EXPECTED_VERSION_MAJOR == 1);
  CHECK(ZEUS_EXPECTED_VERSION_MINOR == 4);
  CHECK(ZEUS_EXPECTED_VERSION_PATCH == 0);

  return failures == 0 ? 0 : 1;
}
