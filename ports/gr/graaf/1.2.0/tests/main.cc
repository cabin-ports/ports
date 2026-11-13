#include <graaflib/algorithm/clique_detection/bron_kerbosch.h>
#include <graaflib/algorithm/coloring/greedy_graph_coloring.h>
#include <graaflib/algorithm/coloring/welsh_powell.h>
#include <graaflib/algorithm/cycle_detection/dfs_cycle_detection.h>
#include <graaflib/algorithm/graph_traversal/breadth_first_search.h>
#include <graaflib/algorithm/graph_traversal/depth_first_search.h>
#include <graaflib/algorithm/minimum_spanning_tree/kruskal.h>
#include <graaflib/algorithm/minimum_spanning_tree/prim.h>
#include <graaflib/algorithm/shortest_path/a_star.h>
#include <graaflib/algorithm/shortest_path/bellman_ford.h>
#include <graaflib/algorithm/shortest_path/bfs_shortest_path.h>
#include <graaflib/algorithm/shortest_path/dijkstra_shortest_path.h>
#include <graaflib/algorithm/shortest_path/dijkstra_shortest_paths.h>
#include <graaflib/algorithm/shortest_path/floyd_warshall.h>
#include <graaflib/algorithm/strongly_connected_components/kosaraju.h>
#include <graaflib/algorithm/strongly_connected_components/tarjan.h>
#include <graaflib/algorithm/topological_sorting/dfs_topological_sorting.h>
#include <graaflib/graph.h>
#include <graaflib/io/dot.h>
#include <graaflib/properties/vertex_properties.h>
#include <graaflib/tree.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <list>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

namespace alg = graaf::algorithm;
using graaf::vertex_id_t;

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// A weighted undirected graph whose shortest path a-e is a-c-f-e (20).
struct Roads {
  graaf::undirected_graph<char, int> graph;
  vertex_id_t a, b, c, d, e, f;

  Roads() {
    a = graph.add_vertex('a');
    b = graph.add_vertex('b');
    c = graph.add_vertex('c');
    d = graph.add_vertex('d');
    e = graph.add_vertex('e');
    f = graph.add_vertex('f');
    graph.add_edge(a, b, 7);
    graph.add_edge(a, c, 9);
    graph.add_edge(a, f, 14);
    graph.add_edge(b, c, 10);
    graph.add_edge(b, d, 15);
    graph.add_edge(c, d, 11);
    graph.add_edge(c, f, 2);
    graph.add_edge(d, e, 6);
    graph.add_edge(e, f, 9);
  }
};

int TreeCost(const graaf::undirected_graph<char, int>& graph,
             const std::vector<graaf::edge_id_t>& tree) {
  int cost = 0;
  for (const auto& edge : tree) cost += graph.get_edge(edge);
  return cost;
}

void TestUndirected() {
  Roads roads;
  const auto& g = roads.graph;
  check(g.vertex_count() == 6 && g.edge_count() == 9 && g.is_undirected(),
        "six vertices, nine edges");
  check(g.has_edge(roads.f, roads.c) && !g.has_edge(roads.a, roads.e),
        "has_edge either way round");

  const auto path = alg::dijkstra_shortest_path(g, roads.a, roads.e);
  check(path && path->total_weight == 20 &&
            path->vertices ==
                std::list<vertex_id_t>{roads.a, roads.c, roads.f, roads.e},
        "dijkstra a-c-f-e, 20");
  const auto star = alg::a_star_search(g, roads.a, roads.e,
                                       [](vertex_id_t) { return 0; });
  check(star && path && *star == *path, "a* with a zero heuristic");

  const auto all = alg::dijkstra_shortest_paths(g, roads.a);
  check(all.size() == 6 && all.at(roads.e).total_weight == 20 &&
            all.at(roads.d).total_weight == 20 &&
            all.at(roads.f).total_weight == 11,
        "dijkstra from a to every vertex");
  // Ids 0..5 are a..f, the matrix's order.
  const auto matrix = alg::floyd_warshall_shortest_paths(g);
  check(matrix.size() == 6 && matrix[roads.a][roads.e] == 20 &&
            matrix[roads.e][roads.a] == 20 && matrix[roads.b][roads.f] == 12,
        "floyd-warshall");
  // Fewest edges: a-f-e.
  const auto hops = alg::bfs_shortest_path(g, roads.a, roads.e);
  check(hops && hops->total_weight == 2 &&
            hops->vertices == std::list<vertex_id_t>{roads.a, roads.f, roads.e},
        "bfs shortest path a-f-e");
  check(graaf::properties::vertex_degree(g, roads.c) == 4, "degree of c");

  const auto kruskal = alg::kruskal_minimum_spanning_tree(g);
  check(kruskal.size() == 5 && TreeCost(g, kruskal) == 33, "kruskal 33");
  const auto prim = alg::prim_minimum_spanning_tree(g, roads.a);
  check(prim && prim->size() == 5 && TreeCost(g, *prim) == 33, "prim 33");

  const auto colors = alg::greedy_graph_coloring(g);
  bool proper = colors.size() == 6;
  for (const auto& [edge, weight] : g.get_edges())
    proper = proper && colors.at(edge.first) != colors.at(edge.second);
  check(proper, "greedy coloring is proper");
  // Welsh-Powell colours the highest degree first: on the path x-y-z, y gets 0
  // and both ends 1.
  graaf::undirected_graph<char, int> line;
  const auto x = line.add_vertex('x');
  const auto y = line.add_vertex('y');
  const auto z = line.add_vertex('z');
  line.add_edge(x, y, 1);
  line.add_edge(y, z, 1);
  check(alg::welsh_powell_coloring(line) ==
            std::unordered_map<vertex_id_t, int>{{x, 1}, {y, 0}, {z, 1}},
        "welsh-powell on a path");

  std::unordered_set<vertex_id_t> reached{roads.a};
  alg::breadth_first_traverse(g, roads.a, [&](const graaf::edge_id_t& edge) {
    reached.insert(edge.second);
  });
  check(reached.size() == 6, "bfs reaches every vertex");
  // DFS reports only tree edges: five for six connected vertices.
  std::unordered_set<vertex_id_t> visited{roads.a};
  int tree_edges = 0;
  alg::depth_first_traverse(g, roads.a, [&](const graaf::edge_id_t& edge) {
    check(visited.contains(edge.first), "dfs edge starts at a visited vertex");
    visited.insert(edge.second);
    ++tree_edges;
  });
  check(visited.size() == 6 && tree_edges == 5, "dfs spanning tree");

  // Without f, the best route is a-c-d-e (26).
  Roads cut;
  cut.graph.remove_vertex(cut.f);
  const auto detour = alg::dijkstra_shortest_path(cut.graph, cut.a, cut.e);
  check(cut.graph.edge_count() == 6 && detour && detour->total_weight == 26 &&
            detour->vertices ==
                std::list<vertex_id_t>{cut.a, cut.c, cut.d, cut.e},
        "remove_vertex drops its edges");
}

// A user edge type: weights come from graaf::weighted_edge::get_weight.
class Track : public graaf::weighted_edge<double> {
 public:
  explicit Track(double km) : km_(km) {}
  [[nodiscard]] double get_weight() const noexcept override { return km_; }

 private:
  double km_;
};

void TestWeightedEdgeType() {
  graaf::undirected_graph<int, Track> g;
  const auto x = g.add_vertex(1);
  const auto y = g.add_vertex(2);
  const auto z = g.add_vertex(3);
  g.add_edge(x, y, Track{1.5});
  g.add_edge(y, z, Track{2.25});
  g.add_edge(x, z, Track{5.0});
  const auto path = alg::dijkstra_shortest_path(g, x, z);
  check(path && path->total_weight == 3.75 &&
            path->vertices == std::list<vertex_id_t>{x, y, z},
        "dijkstra over a weighted_edge subclass");
}

void TestDirected() {
  // a->b is negative, so the shortest s-t route is s-a-b-t (6).
  graaf::directed_graph<char, int> g;
  const auto s = g.add_vertex('s');
  const auto a = g.add_vertex('a');
  const auto b = g.add_vertex('b');
  const auto t = g.add_vertex('t');
  g.add_edge(s, a, 4);
  g.add_edge(s, b, 6);
  g.add_edge(a, b, -3);
  g.add_edge(a, t, 8);
  g.add_edge(b, t, 5);
  const auto paths = alg::bellman_ford_shortest_paths(g, s);
  check(paths.size() == 4 && paths.at(b).total_weight == 1 &&
            paths.at(t).total_weight == 6 &&
            paths.at(t).vertices == std::list<vertex_id_t>{s, a, b, t},
        "bellman-ford with a negative edge");

  // The same graph is a DAG.
  const auto order = alg::dfs_topological_sort(g);
  bool sorted = order && order->size() == 4;
  if (sorted) {
    std::vector<std::size_t> position(4);
    for (std::size_t i = 0; i < order->size(); ++i)
      position.at((*order)[i]) = i;
    for (const auto& [edge, weight] : g.get_edges())
      sorted = sorted && position.at(edge.first) < position.at(edge.second);
  }
  check(sorted, "topological order respects every edge");
  check(graaf::properties::vertex_outdegree(g, s) == 2 &&
            graaf::properties::vertex_indegree(g, t) == 2,
        "out- and in-degree");
  check(!alg::dfs_cycle_detection(g), "no cycle");
  g.add_edge(t, s, 1);
  check(alg::dfs_cycle_detection(g) && !alg::dfs_topological_sort(g),
        "a back edge makes a cycle");

  // A fresh directory per run, so concurrent runs cannot clash.
  std::random_device random;
  std::filesystem::path dir;
  do {
    dir = std::filesystem::temp_directory_path() /
          ("graaf-test-" + std::to_string(random()));
  } while (!std::filesystem::create_directory(dir));
  const auto dot = dir / "graph.dot";
  graaf::io::to_dot(g, dot);
  std::ostringstream dot_text;
  {
    std::ifstream in(dot);
    dot_text << in.rdbuf();
  }
  std::filesystem::remove_all(dir);
  const std::string text = dot_text.str();
  check(text.rfind("digraph {\n", 0) == 0 &&
            text.find("\t1 -> 2 [label=\"-3\"];\n") != std::string::npos &&
            text.size() > 2 && text.compare(text.size() - 2, 2, "}\n") == 0,
        "dot output");
}

std::vector<std::vector<vertex_id_t>> Normalized(
    std::vector<std::vector<vertex_id_t>> groups) {
  for (auto& group : groups) std::sort(group.begin(), group.end());
  std::sort(groups.begin(), groups.end());
  return groups;
}

void TestComponentsAndCliques() {
  // Cycles 0-1-2 and 3-4, joined one way by 2->3, and a lone vertex 5.
  graaf::directed_graph<int, int> g;
  for (int i = 0; i < 6; ++i)
    check(g.add_vertex(i) == static_cast<vertex_id_t>(i),
          "vertex ids count up from 0");
  g.add_edge(0, 1, 1);
  g.add_edge(1, 2, 1);
  g.add_edge(2, 0, 1);
  g.add_edge(2, 3, 1);
  g.add_edge(3, 4, 1);
  g.add_edge(4, 3, 1);
  const std::vector<std::vector<vertex_id_t>> sccs{{0, 1, 2}, {3, 4}, {5}};
  check(Normalized(alg::tarjans_strongly_connected_components(g)) == sccs,
        "tarjan components");
  check(Normalized(alg::kosarajus_strongly_connected_components(g)) == sccs,
        "kosaraju components");

  // The 4-clique 0-1-2-3 plus the edge 3-4.
  graaf::undirected_graph<int, int> u;
  for (int i = 0; i < 5; ++i)
    check(u.add_vertex(i) == static_cast<vertex_id_t>(i),
          "undirected vertex ids count up from 0");
  for (vertex_id_t i = 0; i < 4; ++i)
    for (vertex_id_t j = i + 1; j < 4; ++j) u.add_edge(i, j, 1);
  u.add_edge(3, 4, 1);
  const std::vector<std::vector<vertex_id_t>> cliques{{0, 1, 2, 3}, {3, 4}};
  check(Normalized(alg::bron_kerbosch(u)) == cliques, "maximal cliques");
}

void TestTree() {
  graaf::tree<int, char> tree(1);
  auto* child = tree.root()->add_child('x', 2);
  auto* grandchild = child->add_child('y', 3);
  check(tree.root()->children.size() == 1 &&
            tree.root()->children[0].value == 'x' &&
            tree.root()->children[0].child.get() == child &&
            child->parent == tree.root() && child->value == 2 &&
            grandchild->parent == child && grandchild->value == 3,
        "tree links");
}

}  // namespace

int main() {
  TestUndirected();
  TestWeightedEdgeType();
  TestDirected();
  TestComponentsAndCliques();
  TestTree();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("graaf consumer test passed");
  return 0;
}
