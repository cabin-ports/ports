// Starts itself as two MPI processes under mpiexec.  The two exchange a
// serialized value point to point, then reduce, broadcast and gather
// across the communicator; each process checks what it receives.
#include <boost/mpi.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace mpi = boost::mpi;

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

int process(int argc, char** argv) {
  mpi::environment environment(argc, argv);
  mpi::communicator world;
  const int rank = world.rank();
  check(world.size() == 2, "two processes");
  if (!ok) return 1;

  if (rank == 0) {
    world.send(1, 7, std::vector<std::string>{"cabin", "ports"});
    std::string joined;
    world.recv(1, 8, joined);
    check(joined == "cabin ports", "point to point");
  } else {
    std::vector<std::string> words;
    world.recv(0, 7, words);
    check(words.size() == 2, "a serialized vector arrives");
    world.send(0, 8, words.size() == 2 ? words[0] + " " + words[1] : std::string());
  }

  int sum = 0;
  mpi::all_reduce(world, (rank + 1) * 10, sum, std::plus<int>());
  check(sum == 30, "all_reduce");

  std::string name = rank == 0 ? "root" : "";
  mpi::broadcast(world, name, 0);
  check(name == "root", "broadcast");

  std::vector<int> ranks;
  mpi::gather(world, rank, ranks, 0);
  if (rank == 0) check(ranks == std::vector<int>({0, 1}), "gather");

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
  std::puts("boost-mpi: two processes exchanged messages and agreed on the results");
  return 0;
}
