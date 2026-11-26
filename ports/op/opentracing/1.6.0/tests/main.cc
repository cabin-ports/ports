#include <opentracing/dynamic_load.h>
#include <opentracing/mocktracer/in_memory_recorder.h>
#include <opentracing/mocktracer/tracer.h>
#include <opentracing/noop.h>
#include <opentracing/version.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace ot = opentracing;
namespace mt = opentracing::mocktracer;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
      return 1;                                             \
    }                                                       \
  } while (0)

// The macro a tracer plugin uses to export its factory.
static int MakeFactory(const char*, const char*, const void**, void*, void**) {
  return 42;
}
OPENTRACING_DECLARE_IMPL_FACTORY(MakeFactory)

struct MapCarrier : ot::TextMapReader, ot::TextMapWriter {
  explicit MapCarrier(std::map<std::string, std::string>& m) : map(m) {}

  ot::expected<void> Set(ot::string_view key,
                         ot::string_view value) const override {
    map[key] = value;
    return {};
  }

  ot::expected<void> ForeachKey(
      std::function<ot::expected<void>(ot::string_view, ot::string_view)> f)
      const override {
    for (const auto& kv : map) {
      auto r = f(kv.first, kv.second);
      if (!r) return r;
    }
    return {};
  }

  std::map<std::string, std::string>& map;
};

int main() {
  CHECK(std::strcmp(OPENTRACING_VERSION, "1.6.0") == 0);
  CHECK(std::strcmp(OPENTRACING_ABI_VERSION, "3") == 0);

  // Noop tracer: spans work but record nothing and cannot be injected.
  auto noop = ot::MakeNoopTracer();
  CHECK(noop);
  auto noop_span = noop->StartSpan("noop");
  CHECK(noop_span);
  noop_span->SetTag("k", 1);
  noop_span->Finish();

  // Mock tracer: finished spans reach the recorder with their tags.
  auto recorder = new mt::InMemoryRecorder{};
  mt::MockTracerOptions options;
  options.recorder.reset(recorder);
  options.propagation_options.propagation_key = "ctx";
  std::shared_ptr<ot::Tracer> tracer{new mt::MockTracer{std::move(options)}};

  auto parent = tracer->StartSpan(
      "parent", {ot::SetTag("answer", 42), ot::SetTag("ok", true),
                 ot::SetTag("name", std::string("span"))});
  parent->SetBaggageItem("user", "ken");
  CHECK(parent->BaggageItem("user") == "ken");

  // Propagate the span context through a text map and continue the trace.
  std::map<std::string, std::string> wire;
  MapCarrier carrier(wire);
  CHECK(tracer->Inject(parent->context(), carrier));
  CHECK(wire.size() == 1);
  CHECK(wire.count("ctx") == 1);
  auto extracted = tracer->Extract(carrier);
  CHECK(extracted);
  CHECK(extracted->get() != nullptr);

  auto child = tracer->StartSpan("child", {ot::ChildOf(extracted->get())});
  child->Finish();
  parent->Finish();

  CHECK(recorder->size() == 2);
  auto spans = recorder->spans();
  CHECK(spans[0].operation_name == "child");
  CHECK(spans[1].operation_name == "parent");
  CHECK(spans[0].span_context.trace_id == spans[1].span_context.trace_id);
  CHECK(spans[0].span_context.span_id != spans[1].span_context.span_id);
  CHECK(spans[0].span_context.baggage.at("user") == "ken");
  CHECK(spans[0].references.size() == 1);
  CHECK(spans[0].references[0].reference_type == ot::SpanReferenceType::ChildOfRef);
  CHECK(spans[0].references[0].span_id == spans[1].span_context.span_id);

  const std::map<std::string, ot::Value> expected_tags = {
      {"answer", 42}, {"ok", true}, {"name", std::string("span")}};
  CHECK(spans[1].tags == expected_tags);

  // A span context from another tracer type cannot be injected.
  auto bad = tracer->Inject(noop_span->context(), carrier);
  CHECK(!bad);
  CHECK(bad.error() == ot::invalid_span_context_error);

  // Dynamic loading is enabled: a missing library reports a load failure.
  std::string message;
  auto handle = ot::DynamicallyLoadTracingLibrary(
      "/nonexistent/libtracer.so", message);
  CHECK(!handle);
  CHECK(handle.error() == ot::dynamic_load_failure_error);
  CHECK(!message.empty());
  CHECK(OpenTracingMakeTracerFactory == &MakeFactory);
  return 0;
}
