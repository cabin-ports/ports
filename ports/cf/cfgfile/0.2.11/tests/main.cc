#include <cfgfile/all.hpp>

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

class item_tag : public cfgfile::tag_no_value_t<> {
public:
  item_tag(const std::string &name, bool mandatory)
      : cfgfile::tag_no_value_t<>(name, mandatory),
        m_name(*this, "name", true), m_qty(*this, "qty", true) {}
  item_tag(const std::string &name, int qty)
      : item_tag("item", true) {
    m_name.set_value(name);
    m_qty.set_value(qty);
    set_defined();
  }
  std::string name() const { return m_name.value(); }
  int qty() const { return m_qty.value(); }

private:
  cfgfile::tag_scalar_t<std::string> m_name;
  cfgfile::tag_scalar_t<int> m_qty;
};

class root_tag : public cfgfile::tag_no_value_t<> {
public:
  root_tag()
      : cfgfile::tag_no_value_t<>("cfg", true), m_title(*this, "title", true),
        m_mode(*this, "mode", true), m_level_constraint(0, 10),
        m_level(*this, "level", true), m_items(*this, "item", false) {
    m_mode_constraint.add_value("a");
    m_mode_constraint.add_value("b");
    m_mode.set_constraint(&m_mode_constraint);
    m_level.set_constraint(&m_level_constraint);
  }

  cfgfile::tag_scalar_t<std::string> m_title;
  cfgfile::tag_scalar_vector_t<std::string> m_mode;
  cfgfile::constraint_one_of_t<std::string> m_mode_constraint;
  cfgfile::constraint_min_max_t<int> m_level_constraint;
  cfgfile::tag_scalar_t<int> m_level;
  cfgfile::tag_vector_of_tags_t<item_tag> m_items;
};

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool parse(const std::string &text, root_tag &tag) {
  std::istringstream in(text);
  try {
    cfgfile::read_cfgfile(tag, in, "test.cfg");
  } catch (const cfgfile::exception_t<> &) {
    return false;
  }
  return true;
}

} // namespace

int main() {
  const std::string good =
      "{cfg\n"
      "  {title \"hello world\\n\"}\n"
      "  {mode a b a}\n"
      "  {level 7}\n"
      "  {item {name one} {qty 1}}\n"
      "  {item {name two} {qty 22}}\n"
      "}\n";

  root_tag cfg;
  check(parse(good, cfg), "parse good config");
  check(cfg.m_title.value() == "hello world\n", "quoted string with escape");
  check(cfg.m_mode.values() == std::vector<std::string>({"a", "b", "a"}),
        "scalar vector");
  check(cfg.m_level.value() == 7, "int value");
  check(cfg.m_items.values().size() == 2, "vector of tags size");
  check(cfg.m_items.values()[1]->name() == "two" &&
            cfg.m_items.values()[1]->qty() == 22,
        "vector of tags content");

  // Write the parsed tags back and read them again.
  std::ostringstream out;
  cfgfile::write_cfgfile(cfg, out);
  root_tag again;
  check(parse(out.str(), again), "re-read written config");
  check(again.m_title.value() == "hello world\n" && again.m_level.value() == 7 &&
            again.m_mode.values().size() == 3 &&
            again.m_items.values().size() == 2 &&
            again.m_items.values()[0]->name() == "one",
        "round trip values");

  // Build a tag in code and write it.
  root_tag built;
  built.m_title.set_value("t");
  built.m_mode.set_value("b");
  built.m_level.set_value(3);
  built.m_items.set_value(std::make_shared<item_tag>("x", 9));
  built.set_defined();
  std::ostringstream out2;
  cfgfile::write_cfgfile(built, out2);
  root_tag built_back;
  check(parse(out2.str(), built_back) && built_back.m_items.values().size() == 1 &&
            built_back.m_items.values()[0]->qty() == 9,
        "write built tag and read it");

  // Constraint violations throw.
  root_tag bad_range;
  check(!parse("{cfg {title t} {mode a} {level 11}}", bad_range),
        "level above max throws");
  root_tag bad_one_of;
  check(!parse("{cfg {title t} {mode c} {level 1}}", bad_one_of),
        "value outside one-of throws");

  // A missing mandatory tag throws.
  root_tag missing;
  check(!parse("{cfg {title t} {mode a}}", missing), "missing level throws");

  return failures == 0 ? 0 : 1;
}
