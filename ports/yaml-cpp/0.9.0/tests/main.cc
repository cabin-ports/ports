#include <yaml-cpp/contrib/graphbuilder.h>
#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static int fail(const char *what) {
  std::fprintf(stderr, "yaml-cpp: %s\n", what);
  return 1;
}

// The header declares this destructor pure virtual and no upstream file
// defines it, so every program deriving a builder must.
YAML::GraphBuilderInterface::~GraphBuilderInterface() {}

// A GraphBuilder implementation (src/contrib/graphbuilder*.cpp) that
// records a flat event trace.
struct Trace {
  typedef std::string Node;
  typedef std::string Sequence;
  typedef std::string Map;
  std::vector<std::string *> owned;
  std::string log;

  ~Trace() {
    for (std::size_t i = 0; i < owned.size(); ++i) delete owned[i];
  }
  std::string *make(const std::string &text) {
    owned.push_back(new std::string(text));
    log += text + ";";
    return owned.back();
  }
  Node *NewNull(const YAML::Mark &, Node *) { return make("null"); }
  Node *NewScalar(const YAML::Mark &, const std::string &, Node *,
                  const std::string &value) {
    return make("scalar " + value);
  }
  Sequence *NewSequence(const YAML::Mark &, const std::string &, Node *) {
    return make("seq");
  }
  void AppendToSequence(Sequence *, Node *node) { log += "+" + *node + ";"; }
  Map *NewMap(const YAML::Mark &, const std::string &, Node *) {
    return make("map");
  }
  void AssignInMap(Map *, Node *key, Node *value) {
    log += *key + "=" + *value + ";";
  }
  Node *AnchorReference(const YAML::Mark &, Node *node) {
    log += "*" + *node + ";";
    return node;
  }
  // Required by GraphBuilder<Impl>, but GraphBuilderAdapter never calls
  // these two.
  void SequenceComplete(Sequence *) {}
  void MapComplete(Map *) {}
};

int main() {
  const char *doc =
      "name: cabin\n"
      "version: 3\n"
      "ratio: 0.25\n"
      "enabled: true\n"
      "tags: [build, test]\n"
      "base: &base {jobs: 4, verbose: false}\n"
      "derived: *base\n";
  YAML::Node root = YAML::Load(doc);
  if (!root.IsMap() || root.size() != 7) return fail("document shape");
  if (root["name"].as<std::string>() != "cabin") return fail("string scalar");
  if (root["version"].as<int>() != 3) return fail("int scalar");
  if (root["ratio"].as<double>() != 0.25) return fail("double scalar");
  if (!root["enabled"].as<bool>()) return fail("bool scalar");
  if (!root["tags"].IsSequence() || root["tags"].size() != 2 ||
      root["tags"][1].as<std::string>() != "test")
    return fail("flow sequence");
  if (root["derived"]["jobs"].as<int>() != 4 ||
      root["derived"]["verbose"].as<bool>())
    return fail("alias");
  if (root["missing"] || root["missing"].as<int>(7) != 7)
    return fail("fallback");

  // Errors: reading a missing key as a string throws InvalidNode, a
  // malformed scalar TypedBadConversion, and malformed YAML a
  // ParserException (test/node/node_test.cpp MissingKey,
  // test/integration/load_node_test.cpp).
  bool thrown = false;
  try {
    root["absent"].as<std::string>();
  } catch (const YAML::InvalidNode &) {
    thrown = true;
  }
  if (!thrown) return fail("missing key accepted");
  thrown = false;
  try {
    YAML::Load("1.5").as<int>();
  } catch (const YAML::TypedBadConversion<int> &) {
    thrown = true;
  }
  if (!thrown) return fail("1.5 converted to int");
  thrown = false;
  try {
    YAML::Load("key: [1, 2\nother: 3\n");
  } catch (const YAML::ParserException &e) {
    thrown = e.mark.line >= 0;
  }
  if (!thrown) return fail("unterminated flow sequence accepted");

  std::vector<YAML::Node> docs = YAML::LoadAll("--- 1\n--- [a, b]\n...\n");
  if (docs.size() != 2 || docs[0].as<int>() != 1 || docs[1].size() != 2)
    return fail("LoadAll");

  // Emitter outputs from test/integration/emitter_test.cpp.
  {
    YAML::Emitter out;
    out << YAML::BeginMap << YAML::Key << "name" << YAML::Value
        << "Ryan Braun" << YAML::Key << "position" << YAML::Value << "3B"
        << YAML::EndMap;
    if (!out.good() || std::string(out.c_str()) !=
                           "name: Ryan Braun\nposition: 3B")
      return fail("block map emit");
  }
  {
    YAML::Emitter out;
    out << YAML::BeginSeq << YAML::Anchor("fred") << YAML::BeginMap
        << YAML::Key << "name" << YAML::Value << "Fred" << YAML::Key << "age"
        << YAML::Value << 42 << YAML::EndMap << YAML::Alias("fred")
        << YAML::EndSeq;
    if (std::string(out.c_str()) !=
        "- &fred\n  name: Fred\n  age: 42\n- *fred")
      return fail("anchor emit");
  }
  {
    // Floating-point output goes through src/fptostring.cpp (dragonbox).
    YAML::Emitter out;
    out.SetFloatPrecision(3);
    out.SetDoublePrecision(2);
    out << YAML::BeginSeq << 3.1425926f << 53.5893 << 2384626.4338
        << 1999926.4338 << 9999926.4338 << YAML::EndSeq;
    if (std::string(out.c_str()) !=
        "- 3.14\n- 54\n- 2.4e+06\n- 2e+06\n- 1e+07")
      return fail("float emit");
  }
  {
    YAML::Emitter out;
    out << YAML::Flow << YAML::BeginSeq << "Larry" << "Curly" << "Moe"
        << YAML::EndSeq;
    if (std::string(out.c_str()) != "[Larry, Curly, Moe]")
      return fail("flow seq emit");
  }
  {
    // A loaded node re-emits to an equivalent document.
    YAML::Node again = YAML::Load(YAML::Dump(root));
    if (again["derived"]["jobs"].as<int>() != 4 ||
        again["tags"][0].as<std::string>() != "build")
      return fail("dump round trip");
  }
  {
    // The node API builds documents too; std::map converts both ways.
    YAML::Node built;
    built["b"] = 2;
    built["a"].push_back("x");
    std::map<std::string, int> m;
    m["one"] = 1;
    built["m"] = m;
    std::stringstream ss;
    ss << built;
    if (ss.str() != "b: 2\na:\n  - x\nm:\n  one: 1")
      return fail("node emit");
    if (YAML::Load(ss.str())["m"].as<std::map<std::string, int> >()["one"] !=
        1)
      return fail("map conversion");
  }
  {
    std::istringstream in("[&x a, *x, {k: ~}]\n");
    YAML::Parser parser(in);
    Trace trace;
    // Through the GraphBuilderInterface overload: the header's
    // BuildGraphOfNextDocument<Impl> convenience template does not
    // compile, as it re-selects itself for GraphBuilder<Impl>.
    YAML::GraphBuilder<Trace> builder(trace);
    if (!YAML::BuildGraphOfNextDocument(parser, builder.AsBuilderInterface()))
      return fail("graph builder returned no root");
    // A map key waits for its value; the alias resolves to the anchored
    // scalar (src/contrib/graphbuilderadapter.cpp).
    if (trace.log != "seq;scalar a;+scalar a;*scalar a;+scalar a;map;"
                     "scalar k;null;scalar k=null;+map;")
      return fail("graph builder trace");
  }
  return 0;
}
