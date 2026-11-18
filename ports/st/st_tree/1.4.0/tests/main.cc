// Consumer test for cabin-ports/st_tree: the three child storage models (raw,
// ordered<> and keyed<>), each traversal order, the tree-related node methods,
// and erase, graft and deep-copy assignment, checked against exact contents.
// Written in C++11.
#include <st_tree.h>

#include <cstdio>
#include <string>

using st_tree::keyed;
using st_tree::ordered;
using st_tree::tree;

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

template <typename Iterator>
static std::string join(Iterator first, Iterator last) {
  std::string out;
  for (; first != last; ++first) out += first->data();
  return out;
}

// T has children U and V; U has W and X; V has Y and Z.
static void build(tree<std::string> &t) {
  t.insert("T");
  t.root().insert("U");
  t.root().insert("V");
  t.root()[0].insert("W");
  t.root()[0].insert("X");
  t.root()[1].insert("Y");
  t.root()[1].insert("Z");
}

static void test_raw() {
  tree<std::string> t;
  build(t);
  check(t.size() == 7 && t.depth() == 3 && t.root().size() == 2, "size and depth");
  check(join(t.begin(), t.end()) == "TUVWXYZ" && join(t.bf_begin(), t.bf_end()) == "TUVWXYZ", "breadth first");
  check(join(t.df_pre_begin(), t.df_pre_end()) == "TUWXVYZ", "depth first, pre-order");
  check(join(t.df_post_begin(), t.df_post_end()) == "WXUYZVT", "depth first, post-order");
  check(join(t.root()[0].begin(), t.root()[0].end()) == "WX", "a node's children");

  const tree<std::string>::node_type &x = t.root()[0][1];
  check(x.data() == "X" && x.ply() == 2 && x.parent().data() == "U" && !x.is_root() && t.root().is_root(),
        "ply, parent and is_root");
  check(t.root()[1].subtree_size() == 3 && t.root()[1].depth() == 2, "a subtree's size and depth");

  t.root().erase(t.root().begin());  // removes U with W and X
  check(t.size() == 4 && join(t.begin(), t.end()) == "TVYZ", "erase removes the whole subtree");
}

static void test_copy_and_graft() {
  tree<std::string> t;
  build(t);
  tree<std::string> other;
  other.insert("A");
  other.root().insert("B");

  other.root()[0] = t.root()[1];  // deep copy of V, Y, Z over B
  check(other.size() == 4 && join(other.begin(), other.end()) == "AVYZ" && t.size() == 7, "assignment copies");

  other.root().graft(t.root()[0]);  // moves U, W, X out of t
  check(other.size() == 7 && join(other.df_pre_begin(), other.df_pre_end()) == "AVYZUWX", "graft inserts the subtree");
  check(t.size() == 4 && join(t.begin(), t.end()) == "TVYZ", "graft removes it from the source");
}

static void test_ordered() {
  tree<int, ordered<> > t;
  t.insert(0);
  t.root().insert(5);
  t.root().insert(1);
  t.root().insert(3);
  t.root().insert(3);
  std::string order;
  for (tree<int, ordered<> >::node_type::iterator j = t.root().begin(); j != t.root().end(); ++j)
    order += std::to_string(j->data()) + " ";
  check(order == "1 3 3 5 " && t.root().count(3) == 2, "ordered<> keeps children sorted, duplicates included");
}

static void test_keyed() {
  typedef tree<int, keyed<std::string> > keyed_tree;
  keyed_tree t;
  t.insert(0);
  t.root().insert("C", 3);
  t.root().insert("A", 1);
  t.root().insert(keyed_tree::node_type::kv_pair("B", 2));
  bool inserted = t.root().insert("A", 99).second;  // ignored, as with map<>
  t.root()["C"].insert("D", 4);
  std::string visit;
  for (keyed_tree::iterator j = t.begin(); j != t.end(); ++j) visit += j->key() + "=" + std::to_string(j->data()) + " ";
  check(!inserted && visit == "=0 A=1 B=2 C=3 D=4 ", "keyed<> orders children by key and ignores a duplicate key");
  check(t.root()["B"].data() == 2 && t.root().count("A") == 1 && t.root().count("Q") == 0, "keyed lookup");
}

int main() {
  test_raw();
  test_copy_and_graft();
  test_ordered();
  test_keyed();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("st_tree 1.4.0 C++ ok");
  return 0;
}
