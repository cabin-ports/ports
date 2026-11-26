// Runs Boost.Compute's algorithms on the default OpenCL device - sort,
// transform through a lambda and through a BOOST_COMPUTE_FUNCTION, and
// accumulate - and checks each result against the same computation on the
// host.  Without a device, default_device() throws and the probe fails.
#include <boost/compute/algorithm/accumulate.hpp>
#include <boost/compute/algorithm/copy.hpp>
#include <boost/compute/algorithm/sort.hpp>
#include <boost/compute/algorithm/transform.hpp>
#include <boost/compute/command_queue.hpp>
#include <boost/compute/container/vector.hpp>
#include <boost/compute/context.hpp>
#include <boost/compute/function.hpp>
#include <boost/compute/lambda.hpp>
#include <boost/compute/system.hpp>

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <vector>

namespace compute = boost::compute;

BOOST_COMPUTE_FUNCTION(int, add_four, (int x), { return x + 4; });

int main() {
  compute::device device = compute::system::default_device();
  std::printf("device: %s, platform: %s, %s\n", device.name().c_str(),
              device.platform().name().c_str(), device.version().c_str());
  compute::context context(device);
  compute::command_queue queue(context, device);

  std::vector<int> host(10000);
  for (std::size_t i = 0; i < host.size(); ++i) {
    host[i] = static_cast<int>(i * 7919 % 10007) - 5000;
  }

  compute::vector<int> values(host.begin(), host.end(), queue);
  compute::sort(values.begin(), values.end(), queue);
  using compute::lambda::_1;
  compute::transform(values.begin(), values.end(), values.begin(), _1 * 3 - 1, queue);
  compute::transform(values.begin(), values.end(), values.begin(), add_four, queue);
  const int sum = compute::accumulate(values.begin(), values.end(), 0, queue);
  std::vector<int> result(host.size());
  compute::copy(values.begin(), values.end(), result.begin(), queue);

  std::sort(host.begin(), host.end());
  for (int& value : host) value = value * 3 - 1 + 4;
  const int expected = std::accumulate(host.begin(), host.end(), 0);

  std::size_t wrong = 0;
  for (std::size_t i = 0; i < host.size(); ++i) wrong += result[i] != host[i];
  if (wrong != 0 || sum != expected) {
    std::fprintf(stderr, "FAIL: %zu of %zu values wrong; sum %d, want %d\n", wrong,
                 host.size(), sum, expected);
    return 1;
  }
  std::printf("boost-compute: %zu values sorted, transformed and summed on the device\n",
              host.size());
  return 0;
}
