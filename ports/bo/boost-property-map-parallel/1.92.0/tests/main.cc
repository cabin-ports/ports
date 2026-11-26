// Starts itself as two MPI processes under mpiexec.  Each keeps the
// values of the keys it owns behind a local_property_map and sends them
// to the other as unsafe_pairs; the receiver settles each against what it
// held for that key with basic_reduce and checks the result.
#include <boost/mpi.hpp>
#include <boost/property_map/function_property_map.hpp>
#include <boost/property_map/parallel/basic_reduce.hpp>
#include <boost/property_map/parallel/local_property_map.hpp>
#include <boost/property_map/parallel/unsafe_serialize.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/serialization/vector.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {

struct group {
  int rank;
};

int process_id(const group& g) { return g.rank; }

// A key's owner and its place there: the two processes take alternate keys.
struct locate {
  typedef std::pair<int, std::size_t> result_type;
  result_type operator()(int key) const { return result_type(key % 2, static_cast<std::size_t>(key / 2)); }
};

const int keys = 8;

double value_of(int key) { return 1.5 * key; }

int process(int argc, char** argv) {
  boost::mpi::environment environment(argc, argv);
  const boost::mpi::communicator world;
  if (world.size() != 2) {
    std::fprintf(stderr, "FAIL: %d processes, not two\n", world.size());
    return 1;
  }
  const int rank = world.rank(), other = 1 - rank;

  typedef boost::function_property_map<locate, int> global_map;
  typedef boost::iterator_property_map<std::vector<double>::iterator, boost::identity_property_map> storage_map;
  typedef boost::unsafe_pair<int, double> entry;

  std::vector<double> storage(keys / 2);
  const group here = {rank};
  boost::local_property_map<group, global_map, storage_map> owned(here, global_map(),
                                                                 storage_map(storage.begin()));
  std::vector<entry> mine, theirs;
  for (int key = rank; key < keys; key += 2) put(owned, key, value_of(key));
  // Read only once every key is written, so that each must keep its own value.
  for (int key = rank; key < keys; key += 2) mine.push_back(entry(key, get(owned, key)));

  if (rank == 0) {
    world.send(other, 0, mine);
    world.recv(other, 0, theirs);
  } else {
    world.recv(other, 0, theirs);
    world.send(other, 0, mine);
  }

  bool ok = theirs.size() == mine.size();
  const boost::parallel::basic_reduce<double> reduce;
  for (std::size_t i = 0; i < theirs.size(); ++i) {
    const int key = theirs[i].first;
    // What a process holds for another's key before its value arrives.
    const double held = reduce(key);
    ok = ok && key % 2 == other && held == 0.0 && reduce(key, held, theirs[i].second) == value_of(key);
  }
  if (!ok) std::fprintf(stderr, "FAIL: process %d did not receive the other's values\n", rank);
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
  std::puts("boost-property-map-parallel: two processes exchanged the values of the keys each owns");
  return 0;
}
