// Starts itself as two MPI processes under mpiexec.  The two hold one
// distributed graph between them and run Dijkstra's shortest paths across
// it; each checks the distances of the vertices it owns, and together
// they account for every vertex.
#include <boost/graph/use_mpi.hpp>

#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/distributed/adjacency_list.hpp>
#include <boost/graph/distributed/mpi_process_group.hpp>
#include <boost/graph/iteration_macros.hpp>
#include <boost/mpi.hpp>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>

namespace {

using boost::graph::distributed::mpi_process_group;

typedef boost::adjacency_list<boost::vecS, boost::distributedS<mpi_process_group, boost::vecS>,
                              boost::undirectedS, boost::property<boost::vertex_distance_t, int>,
                              boost::property<boost::edge_weight_t, int> >
    Graph;

int process(int argc, char** argv) {
  boost::mpi::environment environment(argc, argv);

  const int count = 6;
  const std::pair<int, int> edges[] = {{0, 1}, {0, 2}, {0, 5}, {1, 2}, {1, 3}, {2, 3}, {2, 5}, {3, 4}, {4, 5}};
  const int weights[] = {7, 9, 14, 10, 15, 11, 2, 6, 9};
  const int from_first[] = {0, 7, 9, 20, 20, 11};

  // Every process gives the same edges; each keeps those of its vertices.
  Graph graph(edges, edges + 9, weights, count);
  boost::dijkstra_shortest_paths(graph, boost::vertex(0, graph),
                                 boost::distance_map(get(boost::vertex_distance, graph)));

  bool ok = true;
  int owned = 0;
  BGL_FORALL_VERTICES(v, graph, Graph) {
    const int id = static_cast<int>(graph.distribution().global(owner(v), local(v)));
    const int distance = get(boost::vertex_distance, graph, v);
    if (distance != from_first[id]) {
      std::fprintf(stderr, "FAIL: vertex %d is %d from vertex 0, not %d\n", id, distance, from_first[id]);
      ok = false;
    }
    ++owned;
  }

  int processes = 0, total = 0;
  const boost::mpi::communicator world;
  boost::mpi::all_reduce(world, 1, processes, std::plus<int>());
  boost::mpi::all_reduce(world, owned, total, std::plus<int>());
  if (processes != 2 || total != count || owned == count) {
    std::fprintf(stderr, "FAIL: %d processes hold %d vertices, %d of them here\n", processes, total, owned);
    ok = false;
  }
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--process") return process(argc, argv);
  const std::string command = "mpiexec -n 2 \"" + std::string(argv[0]) + "\" --process";
  if (std::system(command.c_str()) != 0) {
    std::fprintf(stderr, "FAIL: `%s` did not succeed\n", command.c_str());
    return 1;
  }
  std::puts("boost-graph-parallel: two processes found the shortest paths of a graph they share");
  return 0;
}
