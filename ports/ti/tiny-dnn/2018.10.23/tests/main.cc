// Forward pass with known weights, gradient-descent training of a linear
// layer, and JSON and cereal-binary round trips of the trained network.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "tiny_dnn/tiny_dnn.h"

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

using namespace tiny_dnn;

static bool near(float_t a, float_t b, float_t tol) {
  return std::fabs(a - b) <= tol;
}

static void set_weights(network<sequential> &net, size_t layer,
                        const vec_t &w, const vec_t &b) {
  auto params = net[layer]->weights();
  *params[0] = w;
  *params[1] = b;
}

int main() {
  set_random_seed(1);

  // Weights are stored input-major: W[in * out_size + out].
  network<sequential> fwd;
  fwd << fully_connected_layer(2, 2) << relu_layer(2)
      << fully_connected_layer(2, 1);
  set_weights(fwd, 0, {1, -1, 2, 3}, {0.5f, -10.0f});
  set_weights(fwd, 2, {2, 4}, {1});
  // x = (1, 2): layer 0 = (1*1+2*2+0.5, -1+6-10) = (5.5, -5), relu = (5.5, 0),
  // output = 2*5.5 + 4*0 + 1 = 12.
  vec_t out = fwd.predict(vec_t{1, 2});
  CHECK(out.size() == 1);
  CHECK(near(out[0], 12, 1e-4f));

  // Train y = 2*x0 - x1 + 0.5 from zero weights.
  network<sequential> net;
  net << fully_connected_layer(2, 1);
  set_weights(net, 0, {0, 0}, {0});
  std::vector<vec_t> xs = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}};
  std::vector<vec_t> ys;
  for (const vec_t &x : xs) ys.push_back({2 * x[0] - x[1] + 0.5f});
  gradient_descent opt;
  opt.alpha = 0.05f;
  CHECK(net.fit<mse>(opt, xs, ys, 3, 1500));
  auto trained = net[0]->weights();
  CHECK(near((*trained[0])[0], 2, 2e-2f));
  CHECK(near((*trained[0])[1], -1, 2e-2f));
  CHECK(near((*trained[1])[0], 0.5f, 2e-2f));
  CHECK(near(net.predict(vec_t{3, 1})[0], 5.5f, 5e-2f));
  CHECK(near(net.get_loss<mse>(xs, ys), 0, 1e-3f));

  // The JSON round trip rebuilds the architecture and weights.
  network<sequential> from_text;
  from_text.from_json(net.to_json(content_type::weights_and_model),
                      content_type::weights_and_model);
  CHECK(from_text.layer_size() == 1);
  CHECK(from_text.in_data_size() == 2 && from_text.out_data_size() == 1);
  CHECK(from_text.predict(vec_t{3, 1})[0] == net.predict(vec_t{3, 1})[0]);

  // Binary archives carry the weights into a network of the same shape.
  std::stringstream bin;
  {
    cereal::BinaryOutputArchive oa(bin);
    net.to_archive(oa, content_type::weights);
  }
  network<sequential> from_bin;
  from_bin << fully_connected_layer(2, 1);
  {
    cereal::BinaryInputArchive ia(bin);
    from_bin.from_archive(ia, content_type::weights);
  }
  CHECK(from_bin.predict(vec_t{3, 1})[0] == net.predict(vec_t{3, 1})[0]);

  std::puts("ok");
  return 0;
}
