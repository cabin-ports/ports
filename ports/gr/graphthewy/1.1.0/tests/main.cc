#include <graphthewy/GraphthewyModel.hpp>
#include <graphthewy/GraphthewyCycle.hpp>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    using Pairs = std::vector<std::pair<int, int>>;

    graphthewy::UndirectedGraph<int> u{1, 2, 3};
    u.link(1, 2);
    u.link(2, 3);
    check(u.order() == 3, "undirected order");
    check(u.size() == 2, "undirected size");
    check(u.isLinked(1, 2) && u.isLinked(2, 1), "undirected link is symmetric");
    check(u.isLinked(2, 3) && !u.isLinked(1, 3), "undirected unlinked pair");
    check((u.getEdgePairList() == Pairs{{1, 2}, {2, 1}, {2, 3}, {3, 2}}),
          "undirected edge pairs");
    check(!graphthewy::GraphCycle<graphthewy::UndirectedGraph, int>(u).hasCycle(),
          "undirected path has no cycle");
    u.link(3, 1);
    check(u.size() == 3, "undirected size after closing the triangle");
    check(graphthewy::GraphCycle<graphthewy::UndirectedGraph, int>(u).hasCycle(),
          "undirected triangle has a cycle");

    graphthewy::DirectedGraph<std::string> d{"a", "b", "c"};
    d.link("a", "b");
    d.link("b", "c");
    check(d.order() == 3, "directed order");
    check(d.size() == 2, "directed size");
    check(d.isLinked("a", "b") && !d.isLinked("b", "a"), "directed link has a direction");
    check(!d.isLinked("a", "c"), "directed unlinked pair");
    using SPairs = std::vector<std::pair<std::string, std::string>>;
    check((d.getEdgePairList() == SPairs{{"a", "b"}, {"b", "c"}}), "directed edge pairs");
    check(!graphthewy::GraphCycle<graphthewy::DirectedGraph, std::string>(d).hasCycle(),
          "directed chain has no cycle");
    d.link("c", "a");
    check(d.size() == 3, "directed size after closing the loop");
    check(graphthewy::GraphCycle<graphthewy::DirectedGraph, std::string>(d).hasCycle(),
          "directed loop has a cycle");

    return failures == 0 ? 0 : 1;
}
