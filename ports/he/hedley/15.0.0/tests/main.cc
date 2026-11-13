// A C++98 consumer of hedley: C linkage blocks, the C++ cast macros, the
// empty-base layout, and the macros shared with C.
#include <hedley.h>

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

struct Base {
  virtual ~Base() {}
  virtual int id() const { return 1; }
};

struct Derived : Base {
  int id() const { return 2; }
};

struct Empty1 {};
struct Empty2 {};
struct HEDLEY_EMPTY_BASES TwoEmptyBases : Empty1, Empty2 {
  char c;
};

HEDLEY_WARN_UNUSED_RESULT
int parse_digit(char c) { return (c >= '0' && c <= '9') ? c - '0' : -1; }

HEDLEY_DEPRECATED(15)
int old_api() { return 7; }

HEDLEY_DIAGNOSTIC_PUSH
HEDLEY_DIAGNOSTIC_DISABLE_DEPRECATED
int call_old_api() { return old_api(); }
HEDLEY_DIAGNOSTIC_POP

}  // namespace

HEDLEY_BEGIN_C_DECLS
int hedley_c_linkage(int x);
HEDLEY_END_C_DECLS

HEDLEY_C_DECL int hedley_c_linkage(int x) { return x + 1; }

HEDLEY_STATIC_ASSERT(HEDLEY_VERSION == 15, "hedley 15");

int main() {
  const char* text = "hedley";
  Derived derived;
  Base* base = &derived;

  CHECK(HEDLEY_VERSION == 15);
  CHECK(HEDLEY_VERSION_DECODE_MINOR(HEDLEY_VERSION_ENCODE(4, 5, 6)) == 5);
  CHECK(std::strcmp(HEDLEY_STRINGIFY(HEDLEY_CONCAT(foo, bar)), "foobar") == 0);

  CHECK(HEDLEY_STATIC_CAST(int, 2.5) == 2);
  CHECK(HEDLEY_STATIC_CAST(Derived*, base)->id() == 2);
  CHECK(HEDLEY_CONST_CAST(char*, text) == text);
  CHECK(HEDLEY_REINTERPRET_CAST(const unsigned char*, text)[1] == 'e');
  CHECK(HEDLEY_CPP_CAST(int, 7) == 7);
  CHECK(static_cast<const void*>(HEDLEY_NULL) == 0);

  CHECK(HEDLEY_LIKELY(parse_digit('7') == 7));
  CHECK(!HEDLEY_UNLIKELY(parse_digit('x') != -1));
  CHECK(HEDLEY_REQUIRE_CONSTEXPR(6 * 7) == 42);
  CHECK(hedley_c_linkage(41) == 42);
  CHECK(call_old_api() == 7);
  // GCC and Clang always put both empty bases at offset zero; MSVC does so
  // only under HEDLEY_EMPTY_BASES, its __declspec(empty_bases).
  CHECK(sizeof(TwoEmptyBases) == sizeof(char));

#if defined(_MSC_VER)
  CHECK(HEDLEY_MSVC_VERSION_CHECK(19, 0, 0));
#elif defined(__GNUC__)
  CHECK(HEDLEY_GNUC_VERSION_CHECK(__GNUC__, __GNUC_MINOR__, 0));
#endif

  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("hedley (C++): ok\n");
  return 0;
}
