// Checks LEMON's shortest paths, maximum flow, path and map containers on a
// small network, then the parts that use the compiled library: the global
// random generator, ArgParser, the color constants, the LP interface's
// columns over its no-op solver, and Timer.  C++98, the interface floor.
#include <cstdio>
#include <string>

#include <lemon/arg_parser.h>
#include <lemon/color.h>
#include <lemon/core.h>
#include <lemon/dijkstra.h>
#include <lemon/list_graph.h>
#include <lemon/lp_skeleton.h>
#include <lemon/path.h>
#include <lemon/preflow.h>
#include <lemon/random.h>
#include <lemon/time_measure.h>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "failed: %s\n", what);
    ++failures;
  }
}

typedef lemon::ListDigraph Graph;

}  // namespace

int main() {
  // s->a->b->t is the shortest route (1 + 2 + 1); s->a and s->b carry all
  // of the maximum flow (3 + 2).
  Graph g;
  Graph::Node s = g.addNode(), a = g.addNode(), b = g.addNode(),
              t = g.addNode();
  Graph::ArcMap<int> length(g), capacity(g);
  Graph::Arc sa = g.addArc(s, a), sb = g.addArc(s, b), ab = g.addArc(a, b),
             at = g.addArc(a, t), bt = g.addArc(b, t);
  length[sa] = 1; capacity[sa] = 3;
  length[sb] = 4; capacity[sb] = 2;
  length[ab] = 2; capacity[ab] = 1;
  length[at] = 6; capacity[at] = 2;
  length[bt] = 1; capacity[bt] = 3;

  int nodes = 0;
  for (Graph::NodeIt n(g); n != lemon::INVALID; ++n) ++nodes;
  expect(nodes == 4 && lemon::countArcs(g) == 5, "graph size");

  lemon::Dijkstra<Graph, Graph::ArcMap<int> > dijkstra(g, length);
  dijkstra.run(s);
  expect(dijkstra.dist(t) == 4, "shortest distance to t");
  expect(dijkstra.predNode(t) == b, "t is reached from b");
  lemon::ListPath<Graph> route(dijkstra.path(t));
  expect(route.length() == 3 && route.front() == sa && route.back() == bt,
         "shortest route s-a-b-t");

  lemon::Preflow<Graph, Graph::ArcMap<int> > preflow(g, capacity, s, t);
  preflow.run();
  expect(preflow.flowValue() == 5, "maximum flow value");
  expect(preflow.minCut(s) && !preflow.minCut(t), "minimum cut sides");

  lemon::ListPath<Graph> path;
  path.addBack(ab);
  path.addFront(sa);
  path.addBack(bt);
  path.eraseFront();
  expect(path.length() == 2 && path.front() == ab && path.back() == bt,
         "ListPath edits");

  // A non-scalar value type makes NodeMap an ArrayMap; adding a node after
  // the map exists goes through the graph's alteration notifier.
  Graph::NodeMap<std::string> names(g, "unnamed");
  names[s] = "source";
  Graph::Node late = g.addNode();
  expect(names[s] == "source" && names[late] == "",
         "ArrayMap values across a node addition");
  g.erase(late);

  lemon::Random first(42), second(42);
  bool same = true;
  for (int i = 0; i < 1000; ++i) same = same && first() == second();
  expect(same, "equal seeds give equal sequences");
  int die = first.integer(1, 7);
  expect(die >= 1 && die < 7, "integer range");
  lemon::rnd.seed(7);
  double x = lemon::rnd();
  lemon::rnd.seed(7);
  expect(lemon::rnd() == x && x >= 0.0 && x < 1.0, "global generator reseeds");

  const char* argv[] = {"probe", "-n", "5", "-name", "lemon", "-verbose"};
  int n = 0;
  std::string name;
  bool verbose = false;
  lemon::ArgParser args(6, argv);
  args.refOption("n", "a count", n)
      .refOption("name", "a name", name)
      .refOption("verbose", "a flag", verbose)
      .parse();
  expect(n == 5 && name == "lemon" && verbose && args.given("n"),
         "ArgParser options");

  lemon::Color opposite = lemon::distantColor(lemon::DARK_CYAN);
  expect(opposite.red() == 1 && opposite.green() == 0 &&
             opposite.blue() == 0,
         "distantColor of dark cyan");
  expect(lemon::distantBW(lemon::DARK_BLUE).green() == 1 &&
             lemon::distantBW(lemon::YELLOW).green() == 0,
         "distantBW picks white or black");

  lemon::LpSkeleton lp;
  lemon::LpSkeleton::Col u = lp.addCol(), v = lp.addCol();
  lp.addRow(u + v <= 4);
  lp.obj(u + v);
  lp.max();
  int columns = 0;
  for (lemon::LpSkeleton::ColIt c(lp); c != lemon::INVALID; ++c) ++columns;
  expect(columns == 2 && lp.solve() == lemon::LpSkeleton::SOLVED &&
             lemon::LpSkeleton::INF > 1e300,
         "LP columns over the skeleton solver");

  lemon::Timer timer;
  timer.stop();
  expect(timer.realTime() >= 0.0, "Timer real time");

  if (failures != 0) return 1;
  std::puts("coin-lemon OK");
  return 0;
}
