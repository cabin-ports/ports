#include <popl.hpp>

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static void parse(popl::OptionParser& op, std::vector<const char*> args) {
  op.parse(static_cast<int>(args.size()), args.data());
}

int main() {
  // Parsing of every option kind, with non-option arguments.
  {
    popl::OptionParser op("Allowed options");
    auto verbose = op.add<popl::Switch>("v", "verbose", "be verbose");
    auto quiet = op.add<popl::Switch>("q", "quiet", "be quiet");
    int level = 0;
    auto lvl = op.add<popl::Value<int>>("l", "level", "set the level", 23, &level);
    auto dflt = op.add<popl::Value<int>>("d", "depth", "set the depth", 5);
    auto name = op.add<popl::Value<std::string>>("n", "name", "a name to add");
    auto imp = op.add<popl::Implicit<int>>("m", "implicit", "an implicit value", 42);
    auto imp2 = op.add<popl::Implicit<int>>("k", "key", "another implicit", 42);

    parse(op, {"prog", "-v", "-l", "7", "--name=alice", "-n", "bob", "--name",
               "carol", "-m", "--key=9", "rest1", "rest2"});

    CHECK(verbose->is_set() && verbose->count() == 1 && verbose->value());
    CHECK(!quiet->is_set() && quiet->count() == 0);
    CHECK(lvl->is_set() && lvl->count() == 1 && lvl->value() == 7);
    CHECK(level == 7);
    CHECK(!dflt->is_set() && dflt->count() == 0 && dflt->value() == 5);
    CHECK(name->is_set() && name->count() == 3);
    CHECK(name->value(0) == "alice" && name->value(1) == "bob" &&
          name->value(2) == "carol");
    CHECK(imp->is_set() && imp->count() == 1 && imp->value() == 42);
    CHECK(imp2->is_set() && imp2->value() == 9);

    const std::vector<std::string> rest{"rest1", "rest2"};
    CHECK(op.non_option_args() == rest);
    CHECK(op.unknown_options().empty());
    CHECK(op.get_option<popl::Value<int>>("level") == lvl);
    CHECK(op.get_option<popl::Switch>('v') == verbose);
  }

  // An unknown option is collected, not thrown.
  {
    popl::OptionParser op;
    auto verbose = op.add<popl::Switch>("v", "verbose", "be verbose");
    parse(op, {"prog", "--nope", "-v"});
    CHECK(op.unknown_options().size() == 1 && op.unknown_options()[0] == "--nope");
    CHECK(verbose->is_set());
  }

  // Exceptions.
  {
    popl::OptionParser op;
    op.add<popl::Value<int>>("l", "level", "set the level");
    try {
      parse(op, {"prog", "--level"});
      CHECK(false);
    } catch (const popl::invalid_option& e) {
      CHECK(e.error() == popl::invalid_option::Error::missing_argument);
    }
    try {
      parse(op, {"prog", "-l", "abc"});
      CHECK(false);
    } catch (const popl::invalid_option& e) {
      CHECK(e.error() == popl::invalid_option::Error::invalid_argument);
    }
    try {
      op.get_option<popl::Switch>("nope");
      CHECK(false);
    } catch (const std::invalid_argument&) {
    }

    popl::OptionParser req;
    req.add<popl::Value<std::string>, popl::Attribute::required>("f", "file", "input file");
    try {
      parse(req, {"prog"});
      CHECK(false);
    } catch (const popl::invalid_option& e) {
      CHECK(e.error() == popl::invalid_option::Error::missing_option);
    }

    popl::OptionParser unset;
    auto v = unset.add<popl::Value<int>>("l", "level", "set the level");
    try {
      v->value();
      CHECK(false);
    } catch (const std::out_of_range&) {
    }
  }

  // Help text.
  {
    popl::OptionParser op("Allowed options");
    op.add<popl::Switch>("v", "verbose", "be verbose");
    op.add<popl::Value<int>>("l", "level", "set the level", 23);
    op.add<popl::Implicit<int>>("m", "implicit", "an implicit value", 42);
    const std::string help = op.help();
    CHECK(help.find("Allowed options") != std::string::npos);
    CHECK(help.find("--verbose") != std::string::npos);
    CHECK(help.find("be verbose") != std::string::npos);
    CHECK(help.find("--level arg (=23)") != std::string::npos);
    CHECK(help.find("set the level") != std::string::npos);
    CHECK(help.find("--implicit [=arg(=42)]") != std::string::npos);
    CHECK(help.find("an implicit value") != std::string::npos);
  }

  return failures == 0 ? 0 : 1;
}
