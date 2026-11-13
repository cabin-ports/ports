#include <CXXGraph/CXXGraph.hpp>

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

using Node = CXXGraph::Node<int>;

}  // namespace

int main() {
  // A weighted undirected graph whose shortest path a-e is a-c-f-e (20).
  Node a("a", 1), b("b", 2), c("c", 3), d("d", 4), e("e", 5), f("f", 6);
  CXXGraph::T_EdgeSet<int> roads;
  CXXGraph::id_t id = 0;
  auto road = [&](const Node& x, const Node& y, double weight) {
    roads.insert(std::make_shared<CXXGraph::UndirectedWeightedEdge<int>>(
        ++id, x, y, weight));
  };
  road(a, b, 7);
  road(a, c, 9);
  road(a, f, 14);
  road(b, c, 10);
  road(b, d, 15);
  road(c, d, 11);
  road(c, f, 2);
  road(d, e, 6);
  road(e, f, 9);
  CXXGraph::Graph<int> graph(roads);

  check(graph.getNodeSet().size() == 6, "six nodes");
  check(graph.isUndirectedGraph(), "undirected");
  check(graph.isConnectedGraph(), "connected");

  const auto dijkstra = graph.dijkstra(a, e);
  check(dijkstra.success && dijkstra.result == 20, "dijkstra distance 20");
  check(dijkstra.path == std::vector<std::string>{"a", "c", "f", "e"},
        "dijkstra path a-c-f-e");

  const auto mst = graph.kruskal();
  check(mst.success && mst.mst.size() == 5 && mst.mstCost == 33,
        "kruskal: 5 edges costing 33");

  const auto bfs = graph.breadth_first_search(a);
  check(bfs.size() == 6 && bfs.front().getUserId() == "a", "bfs from a");

  // A DAG, then the same edges plus one back edge.
  CXXGraph::T_EdgeSet<int> deps;
  auto dep = [&](const Node& x, const Node& y) {
    deps.insert(std::make_shared<CXXGraph::DirectedEdge<int>>(++id, x, y));
  };
  dep(a, b);
  dep(a, c);
  dep(b, d);
  dep(c, d);
  dep(d, e);
  CXXGraph::Graph<int> dag(deps);
  check(dag.isDirectedGraph() && !dag.isCyclicDirectedGraphDFS(), "acyclic");
  const auto topo = dag.topologicalSort();
  std::map<std::string, std::size_t> position;
  for (std::size_t i = 0; i < topo.nodesInTopoOrder.size(); ++i)
    position[topo.nodesInTopoOrder[i].getUserId()] = i;
  const bool sorted = topo.success && topo.nodesInTopoOrder.size() == 5 &&
                      position.size() == 5 && position.count("a") &&
                      position.count("b") && position.count("c") &&
                      position.count("d") && position.count("e");
  check(sorted, "topological sort of a..e, each once");
  check(sorted && position["a"] < position["b"] &&
            position["a"] < position["c"] && position["b"] < position["d"] &&
            position["c"] < position["d"] && position["d"] < position["e"],
        "topological order respects every edge");
  dep(e, a);
  CXXGraph::Graph<int> cyclic(deps);
  check(cyclic.isCyclicDirectedGraphDFS(), "back edge makes a cycle");

  // Max flow s->t is 9, the cut {s->a, b->t}; the shortest s-t path is
  // s-b-t, 11.
  CXXGraph::T_EdgeSet<int> pipes;
  auto pipe = [&](const Node& x, const Node& y, double capacity) {
    pipes.insert(std::make_shared<CXXGraph::DirectedWeightedEdge<int>>(
        ++id, x, y, capacity));
  };
  Node s("s", 0), t("t", 0);
  pipe(s, a, 4);
  pipe(s, b, 6);
  pipe(a, b, 3);
  pipe(a, t, 8);
  pipe(b, t, 5);
  CXXGraph::Graph<int> network(pipes);
  check(network.fordFulkersonMaxFlow(s, t) == 9, "max flow 9");
  const auto bellman = network.bellmanford(s, t);
  check(bellman.success && !bellman.negativeCycle && bellman.result == 11,
        "bellman-ford distance 11");

  // HDRF puts each of the 9 edges in exactly one of 4 partitions.
  const auto partitions =
      CXXGraph::Partitioning::Partitioner<int>::partitionGraph(
          graph, CXXGraph::Partitioning::PartitionAlgorithm::HDRF_ALG, 4, 1,
          0.001, 0.0, 2);
  std::map<CXXGraph::id_t, int> placements;
  for (const auto& entry : partitions)
    for (const auto& edge : entry.second->getEdgeSet())
      ++placements[edge->getId()];
  bool once = placements.size() == roads.size();
  for (const auto& edge : roads)
    once = once && placements.count(edge->getId()) == 1 &&
           placements[edge->getId()] == 1;
  check(partitions.size() == 4, "4 partitions");
  check(once, "every edge partitioned once");
  for (unsigned int i = 0; i < 4; ++i)
    check(partitions.count(i) == 1 && partitions.at(i)->getPartitionId() == i,
          "partition id " + std::to_string(i));

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("cxxgraph consumer test passed");
  return 0;
}
