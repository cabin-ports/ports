// simdjson from C++17.  Every kernel compiled into the library that this
// host can run (westmere, haswell and icelake on x86-64, arm64 on
// AArch64, fallback where no SIMD kernel can always run) is made the
// active implementation in turn and must validate, minify and parse the
// same inputs to the same results through the DOM API and parse_many.
// Then the On-Demand front end reads the document, long numbers take the
// fast_float slow path, and the Dragonbox serializer prints fixed text.
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

#include <simdjson.h>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), __LINE__, #cond)

constexpr std::string_view kDoc = R"({
  "name": "caf\u00e9 \ud83d\ude00\n",
  "big": 9007199254740993,
  "max": 18446744073709551615,
  "neg": -42,
  "values": [0.5, 1.25, -3.75, 1e2],
  "flags": {"ok": true, "nothing": null}
})";

// The text minifier drops the whitespace outside strings and leaves the
// escapes as written.
constexpr std::string_view kMinified =
    R"({"name":"caf\u00e9 \ud83d\ude00\n","big":9007199254740993,)"
    R"("max":18446744073709551615,"neg":-42,"values":[0.5,1.25,-3.75,1e2],)"
    R"("flags":{"ok":true,"nothing":null}})";

// kDoc's "name", unescaped: U+00E9 and the surrogate pair for U+1F600 as
// UTF-8.
constexpr std::string_view kName = "caf\xc3\xa9 \xf0\x9f\x98\x80\n";

void check_kernel(const simdjson::implementation* impl) {
  simdjson::get_active_implementation() = impl;
  CHECK(simdjson::get_active_implementation()->name() == impl->name());

  CHECK(simdjson::validate_utf8(kName));
  CHECK(!simdjson::validate_utf8(std::string_view("caf\xc3 ")));
  CHECK(!simdjson::validate_utf8(std::string_view("\xed\xa0\x80")));

  const simdjson::padded_string json(kDoc);
  std::string text(json.size(), '\0');
  size_t text_len = 0;
  CHECK(simdjson::minify(json.data(), json.size(), &text[0], text_len) ==
        simdjson::SUCCESS);
  text.resize(text_len);
  CHECK(text == kMinified);

  // A parser allocated now builds its tape with the active kernel.
  simdjson::dom::parser parser;
  simdjson::dom::element doc;
  CHECK(parser.parse(json).get(doc) == simdjson::SUCCESS);
  std::string_view name;
  CHECK(doc["name"].get(name) == simdjson::SUCCESS && name == kName);
  int64_t big = 0;
  CHECK(doc["big"].get(big) == simdjson::SUCCESS && big == 9007199254740993);
  uint64_t max = 0;
  CHECK(doc["max"].get(max) == simdjson::SUCCESS &&
        max == std::numeric_limits<uint64_t>::max());
  CHECK(doc["max"].get_int64().error() == simdjson::NUMBER_OUT_OF_RANGE);
  int64_t neg = 0;
  CHECK(doc["neg"].get(neg) == simdjson::SUCCESS && neg == -42);
  simdjson::dom::array values;
  CHECK(doc["values"].get(values) == simdjson::SUCCESS && values.size() == 4);
  double sum = 0;
  for (simdjson::dom::element value : values) {
    double d = 0;
    CHECK(value.get(d) == simdjson::SUCCESS);
    sum += d;
  }
  CHECK(sum == 98.0);
  bool ok = false;
  CHECK(doc.at_pointer("/flags/ok").get(ok) == simdjson::SUCCESS && ok);
  simdjson::dom::element nothing;
  CHECK(doc.at_pointer("/flags/nothing").get(nothing) == simdjson::SUCCESS &&
        nothing.is_null());
  CHECK(doc["missing"].error() == simdjson::NO_SUCH_FIELD);
  CHECK(doc["name"].get_int64().error() == simdjson::INCORRECT_TYPE);

  CHECK(parser.parse(simdjson::padded_string(std::string_view("[\"\xff\"]")))
            .error() == simdjson::UTF8_ERROR);
  CHECK(parser.parse(simdjson::padded_string(std::string_view("[\"abc")))
            .error() == simdjson::UNCLOSED_STRING);
  CHECK(parser.parse(simdjson::padded_string(std::string_view(R"({"a":})")))
            .error() != simdjson::SUCCESS);

  // 100 documents in 64-byte batches, so the stream crosses many batch
  // boundaries, on a worker thread except in unoptimized Apple builds.
  std::string lines;
  for (int i = 1; i <= 100; ++i) {
    lines += "{\"id\":" + std::to_string(i) + "}\n";
  }
  const simdjson::padded_string ndjson(lines);
  simdjson::dom::document_stream stream;
  CHECK(parser.parse_many(ndjson, 64).get(stream) == simdjson::SUCCESS);
  int64_t count = 0;
  int64_t id_sum = 0;
  for (auto result : stream) {
    int64_t id = 0;
    CHECK(result["id"].get(id) == simdjson::SUCCESS);
    id_sum += id;
    ++count;
  }
  CHECK(count == 100 && id_sum == 5050);
}

void check_on_demand() {
  simdjson::ondemand::parser parser;
  const simdjson::padded_string json(kDoc);
  simdjson::ondemand::document doc = parser.iterate(json);
  CHECK(std::string_view(doc["name"]) == kName);
  CHECK(int64_t(doc["big"]) == 9007199254740993);
  CHECK(uint64_t(doc["max"]) == std::numeric_limits<uint64_t>::max());
  CHECK(int64_t(doc["neg"]) == -42);
  double sum = 0;
  for (double value : doc["values"]) {
    sum += value;
  }
  CHECK(sum == 98.0);
  CHECK(bool(doc["flags"]["ok"]));
  CHECK(doc["flags"]["nothing"].is_null());
  bool threw = false;
  try {
    static_cast<void>(int64_t(doc["name"]));
  } catch (const simdjson::simdjson_error& e) {
    threw = e.error() == simdjson::INCORRECT_TYPE;
  }
  CHECK(threw);
}

void check_numbers() {
  simdjson::dom::parser parser;
  // 1 + 2^-53 exactly, then one digit above it: more than 19 significant
  // digits take fast_float's slow path, which must round the halfway case
  // to even (1.0) and the next one up to 1 + 2^-52.
  const simdjson::padded_string halfway(
      std::string_view("1.00000000000000011102230246251565404236316680908203125"));
  const simdjson::padded_string above(
      std::string_view("1.00000000000000011102230246251565404236316680908203126"));
  double d = 0;
  CHECK(parser.parse(halfway).get(d) == simdjson::SUCCESS && d == 1.0);
  CHECK(parser.parse(above).get(d) == simdjson::SUCCESS &&
        d == 1.0000000000000002220446049250313080847263336181640625);

  // An integer beyond 64 bits is an error, not a silently rounded double.
  const simdjson::padded_string bigint(
      std::string_view("123456789012345678901234567890"));
  CHECK(parser.parse(bigint).error() == simdjson::BIGINT_ERROR);

  // Dragonbox's shortest round-trip digits, laid out like printf("%g")
  // with a ".0" kept on integral doubles.
  const simdjson::padded_string numbers(std::string_view(
      "[100,1e2,0.1,1e300,5e-324,0.0001,0.00001,-0.0,"
      "1.7976931348623157e308,123456789012345678901234567890.0]"));
  CHECK(simdjson::minify(parser.parse(numbers)) ==
        "[100,100.0,0.1,1e+300,5e-324,0.0001,1e-05,-0.0,"
        "1.7976931348623157e+308,1.2345678901234568e+29]");

  // The serializer escapes quotes, backslashes and control characters
  // and passes other UTF-8 through.
  const simdjson::padded_string escapes(std::string_view(
      R"({"s":"tab\there \"q\" back\\slash \u0001 caf\u00e9"})"));
  CHECK(simdjson::minify(parser.parse(escapes)) ==
        R"({"s":"tab\there \"q\" back\\slash \u0001 caf)"
        "\xc3\xa9\"}");
}

}  // namespace

int main() {
  const auto& available = simdjson::get_available_implementations();
  int kernels = 0;
  for (const simdjson::implementation* impl : available) {
    if (impl->supported_by_runtime_system()) {
      std::printf("kernel %s (%s)\n", impl->name().c_str(),
                  impl->description().c_str());
      check_kernel(impl);
      ++kernels;
    }
  }
  CHECK(kernels > 0);
#if defined(__x86_64__) || defined(_M_X64)
  // A baseline x86-64 build still carries the SSE4.2 and AVX2 kernels,
  // selected at run time: on GCC and Clang each opts into its instruction
  // set per function, and MSVC emits the intrinsics without /arch.
  CHECK(available["westmere"] != nullptr);
  CHECK(available["haswell"] != nullptr);
#endif

  check_on_demand();
  check_numbers();

  if (failures != 0) {
    return 1;
  }
  std::printf("simdjson %s: %d kernels OK\n", SIMDJSON_VERSION, kernels);
  return 0;
}
