// Checks the concepts against types that should and should not satisfy them
// (modelled on Platform.Interfaces.Tests.cpp:39-52 and 66-203) and uses the
// interface base classes at run time.

#include <Platform.Interfaces.h>

#include <algorithm>
#include <concepts>
#include <cstdio>
#include <string>
#include <vector>

using namespace Platform::Interfaces;

namespace {

struct ReadonlyListModel {
  std::vector<int> items{1, 2, 3};
  auto begin() const { return items.begin(); }
  auto end() const { return items.end(); }
  const int &operator[](std::size_t i) const { return items[i]; }
  std::size_t size() const { return items.size(); }
  bool empty() const { return items.empty(); }
};

struct ReadonlySetModel {
  std::vector<int> items{1, 2, 3};
  auto begin() const { return items.begin(); }
  auto end() const { return items.end(); }
  auto find(int item) const { return std::find(begin(), end(), item); }
  bool contains(int item) const { return find(item) != end(); }
  std::size_t size() const { return items.size(); }
  bool empty() const { return items.empty(); }
};

struct IntCounter : ICounter<int> {
  int Count() { return 7; }
};
struct IntArgCounter : ICounter<int, int> {
  int Count(int x) { return x + 1; }
};
struct MinMatcher : IMatcher<int> {
  int minimum;
  explicit MinMatcher(int m) : minimum(m) {}
  bool IsMatched(int c) override { return c >= minimum; }
};
struct IntFactory : IFactory<int> {
  int Create() { return 42; }
};
struct IntProperty : IProperty<int &, int> {
  void Set(int &object, int value) { object = value; }
  int Get(int &object) { return object; }
};
struct EchoCli : ICli {
  int Run(const std::vector<std::string> &args) override {
    return static_cast<int>(args.size());
  }
};
struct NotACounter {
  float Count() { return 0; }
};

static_assert(CLinkAddress<unsigned int>);
static_assert(!CLinkAddress<int>);

static_assert(CReadonlyList<const ReadonlyListModel>);
static_assert(CReadonlyList<const ReadonlyListModel, int>);
static_assert(!CList<const ReadonlyListModel>);
static_assert(std::same_as<ReadonlyList<const ReadonlyListModel>::Item, int>);
static_assert(CReadonlySet<const ReadonlySetModel, int>);
static_assert(!CSet<const ReadonlySetModel>);
static_assert(!CReadonlyList<int>);

static_assert(CCounter<IntCounter, int>);
static_assert(!CCounter<IntCounter, float>);
static_assert(!CCounter<NotACounter, int>);
static_assert(CCounter<IntArgCounter, int, int>);
static_assert(!CCounter<IntArgCounter, float, int>);
static_assert(CMatcher<MinMatcher, int>);
static_assert(CFactory<IntFactory, int>);
static_assert(!CFactory<IntFactory, float>);
static_assert(CProperty<IntProperty, int &, int>);
static_assert(CProvider<IntProperty, int, int &>);
static_assert(CSetter<IntProperty, int, int &>);
static_assert(CCli<EchoCli>);
static_assert(!CCli<IntCounter>);

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

} // namespace

int main() {
  ReadonlyListModel list;
  check(list.size() == 3 && list[2] == 3, "list model");
  check(ReadonlySetModel{}.contains(2) && !ReadonlySetModel{}.contains(9),
        "set model");

  IntCounter counter;
  IntArgCounter arg_counter;
  check(counter.Count() == 7 && arg_counter.Count(4) == 5, "counters");

  MinMatcher matcher(10);
  IMatcher<int> &matcher_if = matcher;
  check(!matcher_if.IsMatched(9) && matcher_if.IsMatched(10), "matcher");

  IntFactory factory;
  check(factory.Create() == 42, "factory");

  IntProperty prop;
  int value = 0;
  prop.Set(value, 5);
  check(prop.Get(value) == 5, "property");

  EchoCli cli;
  ICli &cli_if = cli;
  check(cli_if.Run({"a", "b"}) == 2, "cli");

  return failures == 0 ? 0 : 1;
}
