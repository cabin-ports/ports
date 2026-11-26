// Loads a two-layer Keras 3 style model written by hand (weights in the
// base64 and plain-array forms), checks the embedded test case at load, and
// predicts with exact expected values.  Dense(2, relu) then Dense(1, linear).
#include <fdeep/fdeep.hpp>

#include <cmath>
#include <cstdio>
#include <string>

static const char* const kModelFmt = R"({
  "image_data_format": "channels_last",
  "hash": "hand-written",
  "input_shapes": [[2]],
  "output_shapes": [[1]],
  "architecture": {"config": {"name": "m",
    "input_layers": ["in", 0, 0], "output_layers": ["d2", 0, 0],
    "layers": [
      {"name": "in", "class_name": "InputLayer", "inbound_nodes": [],
       "config": {"batch_shape": [null, 2]}},
      {"name": "d1", "class_name": "Dense",
       "config": {"units": 2, "use_bias": true, "activation": "relu"},
       "inbound_nodes": [{"args": [{"class_name": "__keras_tensor__",
         "config": {"keras_history": ["in", 0, 0]}}], "kwargs": {}}]},
      {"name": "d2", "class_name": "Dense",
       "config": {"units": 1, "use_bias": true, "activation": "linear"},
       "inbound_nodes": [{"args": [{"class_name": "__keras_tensor__",
         "config": {"keras_history": ["d1", 0, 0]}}], "kwargs": {}}]}
    ]}},
  "trainable_params": {
    "d1": {"weights": "AACAPwAAAEAAAEBAAACAQA==", "bias": "AAAAPwAAoMA="},
    "d2": {"weights": [1.0, -1.0], "bias": [0.25]}
  },
  "tests": [{"inputs": [{"shape": [2], "values": [1.0, 2.0]}],
             "outputs": [{"shape": [1], "values": [%s]}]}]
})";

static std::string model_json(const char* expected) {
    char buf[2048];
    std::snprintf(buf, sizeof buf, kModelFmt, expected);
    return buf;
}

static fdeep::tensors input(float a, float b) {
    return {fdeep::tensor(fdeep::tensor_shape(static_cast<std::size_t>(2)),
                          fdeep::float_vec{a, b})};
}

static bool check(const char* what, bool ok) {
    if (!ok) std::fprintf(stderr, "frugally-deep test failed: %s\n", what);
    return ok;
}

int main() {
    bool ok = true;
    // x = [1, 2]: d1 = relu([1*1+2*3+0.5, 1*2+2*4-5]) = [7.5, 5]; d2 = 2.75.
    const auto model = fdeep::read_model_from_string(model_json("2.75"), true, nullptr);
    ok &= check("hash", model.hash() == "hand-written");

    const auto r1 = model.predict(input(1, 2));
    ok &= check("output count", r1.size() == 1);
    ok &= check("output size", r1.at(0).shape().volume() == 1);
    ok &= check("[1,2]", std::fabs(r1.at(0).get(fdeep::tensor_pos(0)) - 2.75f) < 1e-5f);

    // x = [-1, 0.5]: d1 = relu([1.0, -5]) = [1, 0] (relu clamps); d2 = 1.25.
    const auto r2 = model.predict(input(-1, 0.5f));
    ok &= check("[-1,0.5]", std::fabs(r2.at(0).get(fdeep::tensor_pos(0)) - 1.25f) < 1e-5f);
    ok &= check("single output", std::fabs(model.predict_single_output(input(1, 2)) - 2.75f) < 1e-5f);

    // A wrong embedded expectation must fail verification at load.
    bool threw = false;
    try {
        fdeep::read_model_from_string(model_json("9.75"), true, nullptr);
    } catch (const std::exception&) {
        threw = true;
    }
    ok &= check("verification rejects a wrong test case", threw);
    return ok ? 0 : 1;
}
