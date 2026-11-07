// Evaluates a recursive expression tree held in variants, and exercises
// which(), get() and its exception, set(), match(), unary and binary
// visitation, comparison, std::hash, move-only and reference alternatives,
// variant_io's operator<<, variant_cast and the deprecated optional.
#include <mapbox/variant.hpp>
#include <mapbox/variant_cast.hpp>
#include <mapbox/variant_io.hpp>
#include <mapbox/optional.hpp>

#include <cstdio>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>

namespace mu = mapbox::util;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// An expression is a number or an operation on two expressions; the
// operation is only declared here, so it is held in a recursive_wrapper.
struct binary_op;
using expression = mu::variant<double, mu::recursive_wrapper<binary_op>>;

struct binary_op {
  char op;
  expression lhs, rhs;
};

struct evaluate {
  double operator()(double value) const { return value; }
  double operator()(const binary_op& b) const {
    double l = mu::apply_visitor(*this, b.lhs);
    double r = mu::apply_visitor(*this, b.rhs);
    switch (b.op) {
      case '+': return l + r;
      case '-': return l - r;
      case '*': return l * r;
      default: return l / r;
    }
  }
};

void recursive() {
  // (2 + 3) * (10 - 4) / 3
  expression sum = binary_op{'+', 2.0, 3.0};
  expression diff = binary_op{'-', 10.0, 4.0};
  expression product = binary_op{'*', sum, diff};
  expression e = binary_op{'/', product, 3.0};
  check(mu::apply_visitor(evaluate(), e) == 10.0, "expression tree");
  check(e.is<binary_op>() && e.get<binary_op>().op == '/' &&
            e.get<binary_op>().rhs.get<double>() == 3.0,
        "recursive_wrapper unwraps to binary_op");
}

using value = mu::variant<int, double, std::string>;

// One overload per alternative, returning a description.
struct describe {
  std::string operator()(int i) const { return "int " + std::to_string(i); }
  std::string operator()(double) const { return "double"; }
  std::string operator()(const std::string& s) const { return "string " + s; }
};

// Two values add when both are ints and concatenate when both are
// strings; anything else is a mismatch.
struct combine {
  std::string operator()(int a, int b) const { return std::to_string(a + b); }
  std::string operator()(const std::string& a, const std::string& b) const {
    return a + b;
  }
  template <typename A, typename B>
  std::string operator()(const A&, const B&) const {
    return "mismatch";
  }
};

void basics() {
  value i = 7, d = 2.5, s = std::string("seven");
  // which() counts alternatives in declaration order.
  static_assert(value::which<int>() == 0 && value::which<std::string>() == 2,
                "static which()");
  check(i.which() == 0 && d.which() == 1 && s.which() == 2, "which()");
  check(i.is<int>() && !i.is<double>() && s.is<std::string>(), "is<T>()");
  check(i.get<int>() == 7 && mu::get<double>(d) == 2.5 &&
            mu::get_unchecked<std::string>(s) == "seven",
        "get<T>()");

  bool threw = false;
  try {
    (void)i.get<std::string>();
  } catch (const mu::bad_variant_access&) {
    threw = true;
  }
  check(threw, "get<T>() of another alternative throws bad_variant_access");

  value v;
  check(v.valid() && v.is<int>() && v.get<int>() == 0,
        "default construction holds the first alternative");
  v.set<std::string>(3, 'x');
  check(v.is<std::string>() && v.get<std::string>() == "xxx", "set<T>()");
  v = 1.5;
  check(v.is<double>(), "assignment switches alternative");

  check(mu::apply_visitor(describe(), i) == "int 7" &&
            mu::apply_visitor(describe(), s) == "string seven",
        "apply_visitor");
  check(s.match([](int) { return 1; }, [](double) { return 2; },
                [](const std::string& str) { return int(str.size()); }) == 5,
        "match()");
  check(mu::apply_visitor(combine(), value(2), value(40)) == "42" &&
            mu::apply_visitor(combine(), value(std::string("ab")),
                              value(std::string("cd"))) == "abcd" &&
            mu::apply_visitor(combine(), value(1), value(1.0)) == "mismatch",
        "binary apply_visitor");

  // Ordered by alternative first, then by value.
  check(value(1) == value(1) && value(1) != value(2) && value(1) < value(2) &&
            value(100) < value(0.5) && value(0.5) < value(std::string("a")),
        "comparison operators");
  check(std::hash<value>()(s) == std::hash<std::string>()("seven") &&
            std::hash<value>()(i) == std::hash<int>()(7),
        "std::hash hashes the held value");

  static_assert(mu::variant_size<value>::value == 3, "variant_size");
  static_assert(
      std::is_same<mu::variant_alternative_t<1, value>, double>::value,
      "variant_alternative_t");

  std::ostringstream out;
  out << i << ' ' << d << ' ' << s;
  check(out.str() == "7 2.5 seven", "variant_io operator<<");
}

void ownership() {
  mu::variant<std::unique_ptr<int>, int> owner(
      std::unique_ptr<int>(new int(5)));
  auto moved = std::move(owner);
  check(moved.is<std::unique_ptr<int>>() &&
            *moved.get<std::unique_ptr<int>>() == 5,
        "a move-only alternative moves");

  // Visiting an rvalue variant hands the visitor an rvalue alternative,
  // 1.2.0's "moving values out" (variant.hpp:304-310); a copy would not
  // compile.
  struct take {
    std::unique_ptr<int> operator()(std::unique_ptr<int>&& p) const {
      return std::move(p);
    }
    std::unique_ptr<int> operator()(int) const { return nullptr; }
  };
  std::unique_ptr<int> taken = mu::apply_visitor(take(), std::move(moved));
  check(taken != nullptr && *taken == 5, "a value moves out of a visit");

  int target = 1;
  mu::variant<std::reference_wrapper<int>, double> ref(std::ref(target));
  ref.get<int>() = 42;
  check(target == 42, "a reference_wrapper alternative refers to its target");
}

struct shape {
  virtual ~shape() {}
  virtual double area() const = 0;
};
struct square : shape {
  double side = 3;
  double area() const override { return side * side; }
};
struct circle : shape {
  double area() const override { return 3; }
};

void casts() {
  mu::variant<square, circle> sq = square(), ci = circle();
  check(mu::static_variant_cast<shape>(sq).area() == 9 &&
            mu::static_variant_cast<const shape>(ci).area() == 3,
        "static_variant_cast to the common base");
  check(mu::dynamic_variant_cast<square*>(sq) == &sq.get<square>() &&
            mu::dynamic_variant_cast<square*>(ci) == nullptr,
        "dynamic_variant_cast to a pointer");
  const mu::variant<square, circle>& csq = sq;
  check(mu::static_variant_cast<shape>(csq).area() == 9 &&
            &mu::dynamic_variant_cast<square>(csq) == &csq.get<square>(),
        "the casts of a const variant");
  bool threw = false;
  try {
    (void)mu::dynamic_variant_cast<square>(ci);
  } catch (const std::bad_cast&) {
    threw = true;
  }
  check(threw, "dynamic_variant_cast to a wrong reference throws bad_cast");
}

void optional() {
  mu::optional<std::string> o;
  check(!o, "an empty optional is false");
  o = std::string("set");
  check(bool(o) && o.get() == "set", "an assigned optional holds its value");
  // emplace() brace-initializes T (optional.hpp:61-65).
  o.emplace("zz");
  check(*o == "zz", "emplace()");
  o.reset();
  check(!o, "reset()");
}

}  // namespace

int main() {
  // The 1.2.0 tag's header still reports 1.1.0 (variant.hpp:55-59).
  static_assert(VARIANT_MAJOR_VERSION == 1 && VARIANT_MINOR_VERSION == 1 &&
                    VARIANT_PATCH_VERSION == 0 && (VARIANT_VERSION) == 100100,
                "version macros");
  recursive();
  basics();
  ownership();
  casts();
  optional();
  if (failures != 0) return 1;
  std::puts("mapbox-variant C++ consumer ok");
  return 0;
}
