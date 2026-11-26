#include <ryml.hpp>
#include <ryml_std.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,  \
                   #cond);                                                   \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

int main() {
  CHECK(std::strcmp(RYML_VERSION, "0.16.0") == 0);

  // Flow-style parse, deserialize, modify, emit.
  char buf[] = "{foo: 1, bar: [2, 3], john: doe}";
  ryml::Tree tree = ryml::parse_in_place(buf);
  ryml::NodeRef bar = tree["bar"];
  CHECK(tree["foo"].val() == "1");
  CHECK(bar.num_children() == 2);
  CHECK(bar[0].val() == "2");
  CHECK(tree["john"].val() == "doe");

  int b0 = 0, b1 = 0;
  bar[0].load(&b0);
  bar[1].load(&b1);
  CHECK(b0 == 2 && b1 == 3);

  bar[0].save(10);
  bar[1].save(11);
  tree["new"].set_val("node");
  bar.append_child().save(12);
  CHECK(ryml::emitrs_yaml<std::string>(tree) ==
        "{foo: 1,bar: [10,11,12],john: doe,new: node}");
  CHECK(ryml::emitrs_yaml<std::string>(tree["foo"]) == "foo: 1\n");

  // Block style: nesting, sequences of maps, quoting, anchors and references.
  ryml::Tree t2 = ryml::parse_in_arena(
      "name: demo\n"
      "items:\n"
      "  - id: 1\n"
      "    tag: 'a b'\n"
      "  - id: 2\n"
      "    tag: \"c\"\n"
      "base: &anc {x: 5}\n"
      "copy: *anc\n");
  ryml::ConstNodeRef items = t2["items"];
  CHECK(items.num_children() == 2);
  CHECK(items[0]["tag"].val() == "a b");
  CHECK(items[1]["id"].val() == "2");
  CHECK(t2.has_child(t2.root_id(), "name"));
  CHECK(!t2.has_child(t2.root_id(), "absent"));

  t2.resolve();
  int x = 0;
  t2["copy"]["x"].load(&x);
  CHECK(x == 5);

  // std::string and std::vector round trip.
  std::string name;
  t2["name"].load(&name);
  CHECK(name == "demo");
  ryml::Tree t3 = ryml::parse_in_arena("[1, 2, 3, 4]");
  std::vector<int> v;
  t3.rootref().load(&v);
  CHECK(v.size() == 4 && v[0] == 1 && v[3] == 4);
  CHECK(ryml::emitrs_yaml<std::string>(t3) == "[1,2,3,4]");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
