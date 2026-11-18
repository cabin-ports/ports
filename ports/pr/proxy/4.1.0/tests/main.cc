// Consumer test for cabin-ports/proxy: facades built from the documented
// dispatch macros, operator and weak dispatch, and the rtti, as_weak and view
// facilities, checking each call's result and who owns the contained object.
#include <proxy/proxy.h>

#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

PRO_DEF_MEM_DISPATCH(MemAt, at);
PRO_DEF_MEM_DISPATCH(MemArea, Area);
PRO_DEF_MEM_DISPATCH(MemScale, Scale);
PRO_DEF_FREE_DISPATCH(FreeToString, std::to_string, ToString);

struct Dictionary : pro::facade_builder                                  //
                    ::add_convention<MemAt, std::string(int index) const> //
                    ::build {};

struct Shape : pro::facade_builder                                 //
               ::add_convention<MemArea, double() const noexcept> //
               ::add_convention<MemScale, void(double factor)>    //
               ::support_copy<pro::constraint_level::nontrivial>  //
               ::build {};

struct Stringable : pro::facade_builder                           //
                    ::add_convention<FreeToString, std::string()> //
                    ::build {};

struct Streamable
    : pro::facade_builder                                                                          //
      ::add_convention<pro::operator_dispatch<"<<", true>, std::ostream&(std::ostream& out) const> //
      ::build {};

struct WeakDictionary : pro::facade_builder                                                     //
                        ::add_convention<pro::weak_dispatch<MemAt>, std::string(int index) const> //
                        ::build {};

struct RttiAware : pro::facade_builder               //
                   ::add_skill<pro::skills::rtti>    //
                   ::add_skill<pro::skills::as_weak> //
                   ::build {};

struct ResourceDictionary
    : pro::facade_builder                                                                    //
      ::add_convention<MemAt, std::string&(int index), const std::string&(int index) const> //
      ::build {};

class Rectangle {
 public:
  Rectangle(double width, double height) : width_(width), height_(height) { ++live; }
  Rectangle(const Rectangle& other) noexcept : width_(other.width_), height_(other.height_) { ++live; }
  Rectangle& operator=(const Rectangle&) = default;
  ~Rectangle() { --live; }
  double Area() const noexcept { return width_ * height_; }
  void Scale(double factor) {
    width_ *= factor;
    height_ *= factor;
  }
  static int live;

 private:
  double width_;
  double height_;
};
int Rectangle::live = 0;

static int failures = 0;

static void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void test_member_dispatch() {
  std::vector<const char*> words{"hello", "world"};
  pro::proxy<Dictionary> p = &words;  // a raw pointer: the proxy does not own the vector
  check(p->at(0) == "hello" && p->at(1) == "world", "member dispatch through a non-owning proxy");
  words[1] = "there";
  check(p->at(1) == "there", "the proxy sees the pointee's changes");
}

static void test_ownership_and_copy() {
  {
    pro::proxy<Shape> p = pro::make_proxy<Shape, Rectangle>(3, 5);
    check(p.has_value() && p->Area() == 15.0 && Rectangle::live == 1, "make_proxy owns one Rectangle");
    pro::proxy<Shape> q = p;  // support_copy copies the contained object
    q->Scale(2);
    check(q->Area() == 60.0 && p->Area() == 15.0 && Rectangle::live == 2, "a copy is independent");
    p.reset();
    check(!p.has_value() && Rectangle::live == 1, "reset destroys the contained object");
    p = std::move(q);
    check(p->Area() == 60.0 && Rectangle::live == 1, "moving transfers ownership");
  }
  check(Rectangle::live == 0, "destroying the proxy destroys the object");
}

static void test_free_and_operator_dispatch() {
  pro::proxy<Stringable> n = pro::make_proxy<Stringable>(123);
  check(ToString(*n) == "123", "free dispatch through std::to_string");

  static std::string greeting = "Hello";
  pro::proxy<Streamable> s1 = &greeting;
  pro::proxy<Streamable> s2 = std::make_unique<int>(42);
  std::ostringstream out;
  out << *s1 << ' ' << *s2;
  check(out.str() == "Hello 42", "operator<< dispatch with the proxy on the right");
}

static void test_weak_dispatch() {
  std::vector<const char*> words{"hello", "world"};
  pro::proxy<WeakDictionary> p1 = &words;
  check(p1->at(1) == "world", "weak dispatch forwards when the call is valid");
  pro::proxy<WeakDictionary> p2 = pro::make_proxy<WeakDictionary>(123);
  bool threw = false;
  try {
    p2->at(1);
  } catch (const pro::not_implemented&) {
    threw = true;
  }
  check(threw, "weak dispatch throws not_implemented for an int");
}

static void test_rtti_and_weak_proxy() {
  pro::proxy<RttiAware> p1 = pro::make_proxy_shared<RttiAware>(123);
  check(proxy_typeid(*p1) == typeid(int), "proxy_typeid");
  pro::weak_proxy<RttiAware> w = p1;
  proxy_cast<int&>(*p1) = 456;
  pro::proxy<RttiAware> p2 = w.lock();
  p1.reset();
  check(p2.has_value() && proxy_cast<int>(*p2) == 456, "lock shares the object");
  bool threw = false;
  try {
    (void)proxy_cast<double>(*p2);
  } catch (const pro::bad_proxy_cast&) {
    threw = true;
  }
  check(threw, "proxy_cast to the wrong type throws bad_proxy_cast");
  p2.reset();
  check(!w.lock().has_value(), "the weak proxy expires with the last owner");
}

static void test_view() {
  std::map<int, std::string> dict;
  dict[1] = "init";
  pro::proxy_view<ResourceDictionary> v = pro::make_proxy_view<ResourceDictionary>(dict);
  static_assert(std::is_same_v<decltype(v->at(1)), std::string&>, "non-const overload");
  static_assert(std::is_same_v<decltype(std::as_const(v)->at(1)), const std::string&>, "const overload");
  check(std::as_const(v)->at(1) == "init", "the const overload reads");
  v->at(1) = "modified";
  check(dict.at(1) == "modified", "the non-const overload writes through the view");
}

int main() {
  test_member_dispatch();
  test_ownership_and_copy();
  test_free_and_operator_dispatch();
  test_weak_dispatch();
  test_rtti_and_weak_proxy();
  test_view();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("proxy 4.1.0 C++ ok");
  return 0;
}
