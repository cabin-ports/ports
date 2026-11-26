// Each of the six containers inserts, erases and reuses ids.  The plain
// containers recycle an id at once; the versioned ones retire it; the
// controlled ones reset an erased slot to the empty value.
#include <rea.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

template <typename L>
std::vector<int> list_values(const L& l) {
  std::vector<int> out;
  for (auto id = l.id_begin(); !l.id_is_end(id); id = l.id_next(id)) {
    out.push_back(l.id_value(id));
  }
  return out;
}

using ints = std::vector<int>;

}  // namespace

int main() {
  {
    rea::slot_map<int> sm;
    CHECK(sm.size() == 0);
    auto a = sm.insert(10).second;
    auto b = sm.insert(20).second;
    auto c = sm.insert(30).second;
    CHECK(sm.size() == 3);
    sm.erase(b);
    CHECK(sm.size() == 2);
    // The last value moves into the hole; its id still resolves.
    CHECK((ints(sm.begin(), sm.end()) == ints{10, 30}));
    CHECK(sm.id_value(a) == 10);
    CHECK(sm.id_value(c) == 30);
    CHECK(sm.insert(40).second == b);
    CHECK(sm.id_value(b) == 40);
    CHECK((ints(sm.begin(), sm.end()) == ints{10, 30, 40}));
  }
  {
    rea::versioned_slot_map<std::string> sm;
    auto old_id = sm.insert("x").second;
    auto keep = sm.insert("y").second;
    CHECK(sm.id_is_valid(old_id));
    sm.erase(old_id);
    CHECK(!sm.id_is_valid(old_id));
    auto fresh = sm.insert("z").second;
    CHECK(fresh.first == old_id.first);
    CHECK(fresh.second != old_id.second);
    CHECK(sm.id_is_valid(fresh));
    CHECK(!sm.id_is_valid(old_id));
    CHECK(sm.id_value(fresh) == "z");
    CHECK(sm.id_value(keep) == "y");
    CHECK(sm.size() == 2);
  }
  {
    rea::slot_list<int> sl;
    auto a = sl.insert(1);
    auto b = sl.insert(2);
    auto c = sl.insert(3);
    CHECK((list_values(sl) == ints{1, 2, 3}));
    CHECK(sl.erase(b) == c);
    CHECK(sl.size() == 2);
    CHECK((list_values(sl) == ints{1, 3}));
    CHECK(sl.insert(9) == b);
    CHECK((list_values(sl) == ints{1, 3, 9}));
    CHECK(sl.id_value(a) == 1);
    sl.clear();
    CHECK(sl.size() == 0);
    CHECK(sl.id_is_end(sl.id_begin()));
  }
  {
    rea::versioned_slot_list<int> sl;
    auto a = sl.insert(1);
    auto b = sl.insert(2);
    sl.erase(a);
    CHECK(!sl.id_is_valid(a));
    CHECK(sl.id_is_valid(b));
    auto fresh = sl.insert(3);
    CHECK(fresh.first == a.first);
    CHECK(fresh.second != a.second);
    CHECK(!sl.id_is_valid(a));
    CHECK((list_values(sl) == ints{2, 3}));
  }
  {
    rea::controlled_slot_list<std::string> sl;
    auto a = sl.insert("alpha");
    sl.insert("beta");
    CHECK(sl.id_value(a) == "alpha");
    sl.erase(a);
    CHECK(sl.id_value(a).empty());
    CHECK(sl.size() == 1);
    CHECK(sl.insert("gamma") == a);
    CHECK(sl.id_value(a) == "gamma");
  }
  {
    rea::regulated_slot_list<std::string> sl;
    auto a = sl.insert("alpha");
    auto b = sl.insert("beta");
    sl.erase(a);
    CHECK(!sl.id_is_valid(a));
    CHECK(sl.id_is_valid(b));
    CHECK(sl.id_value(a).empty());
    auto fresh = sl.insert("gamma");
    CHECK(fresh.first == a.first);
    CHECK(fresh.second != a.second);
    CHECK(sl.id_value(fresh) == "gamma");
    CHECK(sl.id_value(b) == "beta");
  }
  return failures == 0 ? 0 : 1;
}
