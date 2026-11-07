// Consumer test from C++98: the active alternative, get by type and by index
// and its failure, get_if, holds_alternative, value and variant assignment
// and the lifetime of the value they replace, visit, swap of the same and of
// different alternatives (the former through `using std::swap`, which the
// patch makes C++98-safe), comparison, monostate and variant_size; from
// C++11 on, emplace and visit's deduced result type.
#include <nonstd/variant.hpp>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

// Counts live instances; has no default constructor.
struct Tracked {
  static int live;
  int value;
  explicit Tracked(int v) : value(v) { ++live; }
  Tracked(const Tracked& other) : value(other.value) { ++live; }
  Tracked& operator=(const Tracked& other) {
    value = other.value;
    return *this;
  }
  ~Tracked() { --live; }
};
int Tracked::live = 0;

typedef nonstd::variant<int, std::string> IntOrString;

// An int measures as itself, a string as its length.
struct Measure {
  long operator()(int i) const { return i; }
  long operator()(const std::string& s) const { return static_cast<long>(s.size()); }
};

long measure(const IntOrString& v) {
#if variant_CPP11_OR_GREATER
  return nonstd::visit(Measure(), v);
#else
  return nonstd::visit<long>(Measure(), v);
#endif
}

}  // namespace

int main() {
  CHECK(std::strcmp(variant_lite_VERSION, "3.0.0") == 0);

  // A default-constructed variant holds a value-initialized first alternative.
  IntOrString v;
  CHECK(v.index() == 0 && nonstd::holds_alternative<int>(v) && nonstd::get<int>(v) == 0);
  CHECK(!v.valueless_by_exception());

  // Assignment switches the alternative; get by type and by index agree.
  v = std::string("abc");
  CHECK(v.index() == 1 && nonstd::holds_alternative<std::string>(v));
  CHECK(nonstd::get<std::string>(v) == "abc" && nonstd::get<1>(v) == "abc");
  CHECK(nonstd::get_if<int>(&v) == NULL);
  CHECK(nonstd::get_if<std::string>(&v) != NULL && *nonstd::get_if<std::string>(&v) == "abc");

  // get of an inactive alternative throws bad_variant_access.
  bool thrown = false;
  try {
    (void)nonstd::get<int>(v);
  } catch (const nonstd::bad_variant_access&) {
    thrown = true;
  }
  CHECK(thrown);

  // visit calls the overload for the active alternative.
  CHECK(measure(v) == 3);
  v = IntOrString(42);
  CHECK(measure(v) == 42 && nonstd::get<0>(v) == 42);

  // Assigning a variant replaces the held value, destroying it, or assigns to
  // it when both hold the same alternative.
  typedef nonstd::variant<int, Tracked> IntOrTracked;
  {
    IntOrTracked t(Tracked(7));
    CHECK(Tracked::live == 1 && nonstd::get<Tracked>(t).value == 7);
    t = IntOrTracked(3);
    CHECK(Tracked::live == 0 && nonstd::get<int>(t) == 3);
    t = IntOrTracked(Tracked(8));
    CHECK(Tracked::live == 1 && t.index() == 1);
    const IntOrTracked nine(Tracked(9));
    t = nine;
    CHECK(Tracked::live == 2 && nonstd::get<Tracked>(t).value == 9);
    const IntOrTracked four(4);
    t = four;
    CHECK(Tracked::live == 1 && nonstd::get<int>(t) == 4);
  }
  CHECK(Tracked::live == 0);

  // Assigning a value does the same, except with variant lite's own variant
  // from C++11 on, whose converting assignment never destroys the value it
  // replaces (see cabin.toml); there it only replaces an int.
  {
    IntOrTracked t(3);
    t = Tracked(8);
    CHECK(Tracked::live == 1 && nonstd::get<Tracked>(t).value == 8);
#if !variant_CPP11_OR_GREATER || variant_USES_STD_VARIANT
    t = Tracked(9);
    CHECK(Tracked::live == 1 && nonstd::get<Tracked>(t).value == 9);
    t = 4;
    CHECK(Tracked::live == 0 && nonstd::get<int>(t) == 4);
#endif
  }
  CHECK(Tracked::live == 0);

  // Swap: the same alternative swaps the values, different ones the variants.
  IntOrString left(std::string("left")), right(std::string("right"));
  left.swap(right);
  CHECK(nonstd::get<std::string>(left) == "right" && nonstd::get<std::string>(right) == "left");
  IntOrString five(5);
  nonstd::swap(five, left);
  CHECK(nonstd::get<std::string>(five) == "right" && nonstd::get<int>(left) == 5);

  // Comparison orders by index first, then by value.
  IntOrString one(1), two(2), text(std::string("a"));
  CHECK(one < two && one != two && one == IntOrString(1));
  CHECK(two < text && !(text < two) && text > one);

  // monostate makes a variant whose alternatives are not default-constructible.
  nonstd::variant<nonstd::monostate, Tracked> maybe;
  CHECK(maybe.index() == 0 && nonstd::holds_alternative<nonstd::monostate>(maybe));
  CHECK(Tracked::live == 0);

  CHECK((nonstd::variant_size<nonstd::variant<int, std::string, double> >::value == 3));

#if variant_CPP11_OR_GREATER
  // emplace destroys the held value and constructs the new one in place.
  v.emplace<std::string>(3, 'z');
  CHECK(nonstd::get<std::string>(v) == "zzz" && measure(v) == 3);
  v.emplace<0>(9);
  CHECK(nonstd::get<int>(v) == 9);
  {
    IntOrTracked t(Tracked(1));
    t.emplace<int>(2);
    CHECK(Tracked::live == 0 && nonstd::get<int>(t) == 2);
    t.emplace<Tracked>(3);
    CHECK(Tracked::live == 1 && nonstd::get<1>(t).value == 3);
  }
  CHECK(Tracked::live == 0);
#endif

  if (failures != 0) return 1;
  std::puts("variant-lite: ok");
  return 0;
}
