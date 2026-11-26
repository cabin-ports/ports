#include <sqlite_orm/sqlite_orm.h>

#include <cstdio>
#include <string>

using namespace sqlite_orm;

struct User {
  int id;
  std::string name;
  int age;
};

static int failures = 0;
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

int main() {
  auto storage = make_storage(
      ":memory:",
      make_table("users", make_column("id", &User::id, primary_key()),
                 make_column("name", &User::name),
                 make_column("age", &User::age)));
  storage.sync_schema();
  CHECK(storage.count<User>() == 0);

  CHECK(storage.insert(User{0, "Alice", 30}) == 1);
  CHECK(storage.insert(User{0, "Bob", 25}) == 2);
  CHECK(storage.insert(User{0, "Carol", 35}) == 3);
  CHECK(storage.count<User>() == 3);

  User bob = storage.get<User>(2);
  CHECK(bob.id == 2 && bob.name == "Bob" && bob.age == 25);
  CHECK(storage.get_pointer<User>(99) == nullptr);

  auto older = storage.get_all<User>(where(c(&User::age) > 26),
                                     order_by(&User::age).desc());
  CHECK(older.size() == 2);
  CHECK(older.size() == 2 && older[0].name == "Carol" && older[1].name == "Alice");

  bob.age = 26;
  storage.update(bob);
  CHECK(storage.get<User>(2).age == 26);
  CHECK(storage.count<User>(where(c(&User::age) > 26)) == 2);

  storage.remove<User>(1);
  CHECK(storage.count<User>() == 2);
  CHECK(storage.get_pointer<User>(1) == nullptr);

  storage.begin_transaction();
  storage.insert(User{0, "Dave", 40});
  CHECK(storage.count<User>() == 3);
  storage.rollback();
  CHECK(storage.count<User>() == 2);

  storage.transaction([&] {
    storage.insert(User{0, "Erin", 22});
    return true;
  });
  CHECK(storage.count<User>() == 3);

  return failures == 0 ? 0 : 1;
}
