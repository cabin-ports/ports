#include <ChefFun/Either.hh>
#include <ChefFun/Error.hh>
#include <ChefFun/Option.hh>
#include <ChefFun/Transformers.hh>
#include <ChefFun/Try.hh>

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

} // namespace

int main() {
  using namespace ChefFun;

  CHECK(Option<int>::Some(3).get() == 3);
  CHECK(Option<int>::Some(5).getOrElse(3) == 5);
  CHECK(None<int>().getOrElse(3) == 3);
  CHECK(None<int>().map([](int x) { return x + 1; }).isEmpty());
  CHECK(Option<std::string>::Some(std::string{"abc"})
            .map([](std::string const& s) {
              return static_cast<int>(s.size());
            })
            .get() == 3);
  bool threw = false;
  try {
    (void)Option<int>::None().get();
  } catch (std::invalid_argument const&) {
    threw = true;
  }
  CHECK(threw);

  auto l = Either<int, std::string>::Left(3);
  CHECK(l.isLeft());
  CHECK(!l.isRight());
  CHECK(l.getLeft() == 3);
  threw = false;
  try {
    (void)l.getRight();
  } catch (std::bad_variant_access const&) {
    threw = true;
  }
  CHECK(threw);

  std::string const abc{"abc"};
  auto const r = Either<int, std::string>::Right(abc);
  CHECK(r.isRight());
  CHECK(!r.isLeft());
  CHECK(r.getRight() == "abc");
  threw = false;
  try {
    (void)r.getLeft();
  } catch (std::bad_variant_access const&) {
    threw = true;
  }
  CHECK(threw);

  CHECK(makeSuccessTry(42).getRight() == 42);
  CHECK(tryFn([] { return 5; }).getRight() == 5);
  CHECK(tryFn([]() -> int { throw 7; }).isLeft());
  auto t = tryFn([]() -> int { throw std::runtime_error("error"); });
  CHECK(t.isLeft());
  threw = false;
  try {
    std::rethrow_exception(t.getLeft());
  } catch (std::runtime_error const& e) {
    threw = std::string(e.what()) == "error";
  }
  CHECK(threw);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("chef-fun ok");
  return 0;
}
