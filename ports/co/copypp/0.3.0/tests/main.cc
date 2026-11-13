// Consumer test for cabin-ports/copypp: copies fields between unrelated
// types, through both macros and through arrays and containers, and checks
// the copied values.
#include <icurve/copypp.hh>

#include <array>
#include <cstdio>
#include <list>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Plain structs: the copy is generated from the listed fields.
struct person {
  int id = 0;
  std::string name;
  bool active = false;
};

struct record {
  int id = 0;
  std::string name;
  bool active = false;
  double score = 1.5;
};

COPYPP_FIELDS_NON_INTRUSIVE(record, person, id, name, active)

// Private fields: the copy matches fields by name, type and size at run time.
class account {
public:
  account() = default;
  account(int id, std::string owner, long balance) : id(id), owner(std::move(owner)), balance(balance) {}

private:
  int id = 0;
  std::string owner;
  long balance = 0;

public:
  COPYPP_FIELDS_INTRUSIVE(account, id, owner, balance)
};

class summary {
public:
  int get_id() const { return id; }
  const std::string &get_owner() const { return owner; }
  short get_balance() const { return balance; }

private:
  int id = -1;
  std::string owner = "none";
  short balance = 7; // same name as account::balance, different type

public:
  COPYPP_FIELDS_INTRUSIVE(summary, id, owner, balance)
};

int main() {
  person p{42, "ada", true};
  record r;
  icurve::copy(r, p);
  check(r.id == 42 && r.name == "ada" && r.active && r.score == 1.5,
        "the listed fields are copied and the others are left alone");

  long wide = 0;
  int narrow = 9;
  icurve::copy(wide, narrow);
  check(wide == 9, "a convertible value is assigned");

  account a(5, "grace", 1000);
  summary s;
  icurve::copy(s, a);
  check(s.get_id() == 5 && s.get_owner() == "grace", "fields with matching name and type are copied");
  check(s.get_balance() == 7, "a field whose type differs is skipped");

  person from[3] = {{1, "a", true}, {2, "b", false}, {3, "c", true}};
  record to[2];
  icurve::copy(to, from);
  check(to[0].id == 1 && to[0].name == "a" && to[1].id == 2 && to[1].name == "b" && !to[1].active,
        "arrays copy the shorter length");

  std::vector<person> people = {{7, "x", true}, {8, "y", false}};
  std::list<record> records = {record{}, record{}, record{}};
  icurve::copy(records, people);
  check(records.size() == 2 && records.front().id == 7 && records.front().name == "x" && records.back().id == 8 &&
            !records.back().active,
        "a container with push_back is cleared and refilled");

  std::array<record, 3> fixed{};
  icurve::copy(fixed, people);
  check(fixed[0].id == 7 && fixed[1].name == "y" && fixed[2].id == 0 && fixed[2].name.empty(),
        "a fixed-size range is filled up to the shorter length");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("copypp 0.3.0 C++ ok");
  return 0;
}
