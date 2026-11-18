#define PICOBENCH_IMPLEMENT
#include <picobench/picobench.hpp>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>
#include <string>

namespace pb = picobench;

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// Benchmarks that only add custom durations have exact timings on the real
// clock.  The first sample of each fast dimension is slower, and every sample
// returns its own duration, so the report must keep the fastest sample.
std::map<int, int> fast_seen;

void fast(pb::state& s) {
  const bool first = fast_seen[s.iterations()]++ == 0;
  const int ns = 10 * s.iterations() + (first ? 1000 : 0);
  s.add_custom_duration(ns);
  s.set_result(ns);
}

void slow(pb::state& s) {
  s.add_custom_duration(30 * s.iterations());
  s.set_result(s.iterations());
}

// Timed benchmarks start from a sentinel that the measurement must replace;
// the real clock may report 0 ns or, on a non-steady clock, a negative time.
const std::int64_t unmeasured = INT64_MIN;
int loop_calls = 0;

void loop(pb::state& s) {
  s.add_custom_duration(unmeasured);
  for (auto _ : s) ++loop_calls;
  s.set_result(s.iterations());
}

void scoped(pb::state& s) {
  s.add_custom_duration(unmeasured);
  pb::scope timed(s);
  s.set_result(static_cast<pb::result_t>(s.input_data() * 100 + s.benchmark_user_data()));
}

void registered(pb::state& s) {
  s.add_custom_duration(5 * s.iterations());
  s.set_result(1);
}

void answer_one(pb::state& s) {
  s.add_custom_duration(1);
  s.set_result(1);
}

void answer_two(pb::state& s) {
  s.add_custom_duration(1);
  s.set_result(2);
}

}  // namespace

PICOBENCH_SUITE("global");
PICOBENCH(registered);

namespace {

void test_local_runner() {
  pb::local_runner r;
  r.set_suite("custom");
  r.add_benchmark("slow", slow).iterations({10, 20, 30}).samples(3);
  r.add_benchmark("fast", fast).iterations({10, 20, 30}).samples(2).baseline();
  r.set_suite("clock");
  r.add_benchmark("loop", loop).iterations({100, 200}).samples(2);
  r.add_benchmark("scoped", scoped).inputs({{4, 7}}).user_data(42).samples(1);
  r.run_benchmarks(1);
  const pb::report rep = r.generate_report();
  CHECK(r.error() == pb::no_error && rep.suites.size() == 2);

  const pb::report::suite* custom = rep.find_suite("custom");
  const pb::report::suite* clock = rep.find_suite("clock");
  CHECK(custom != nullptr && clock != nullptr);
  if (custom == nullptr || clock == nullptr) return;
  const pb::report::benchmark* f = custom->find_benchmark("fast");
  const pb::report::benchmark* s = custom->find_benchmark("slow");
  const pb::report::benchmark* l = clock->find_benchmark("loop");
  const pb::report::benchmark* sc = clock->find_benchmark("scoped");
  CHECK(f != nullptr && s != nullptr && l != nullptr && sc != nullptr);
  if (f == nullptr || s == nullptr || l == nullptr || sc == nullptr) return;

  // An explicit baseline wins over the first benchmark; without one, the first is the baseline.
  CHECK(custom->find_baseline() == f && clock->find_baseline() == l);
  CHECK(f->data.size() == 3 && f->data[1].dimension == 20 && f->data[1].samples == 2);
  CHECK(f->data[1].total_time_ns == 200 && f->data[1].result == 200);
  CHECK(s->data[2].dimension == 30 && s->data[2].samples == 3 && s->data[2].total_time_ns == 900);

  CHECK(loop_calls == (100 + 200) * 2);
  CHECK(l->data.size() == 2 && l->data[1].samples == 2 && l->data[1].result == 200);
  CHECK(l->data[0].total_time_ns != unmeasured && l->data[1].total_time_ns != unmeasured);
  CHECK(sc->data.size() == 1 && sc->data[0].dimension == 4 && sc->data[0].result == 742);
  CHECK(sc->data[0].total_time_ns != unmeasured);

  std::ostringstream csv;
  rep.to_csv(csv);
  CHECK(contains(csv.str(), "Suite,Benchmark,b,D,S,\"Total ns\",Result,\"ns/op\",Baseline\n"));
  CHECK(contains(csv.str(), "\"custom\",\"fast\",*,10,2,100,100,10,1.000\n"));
  CHECK(contains(csv.str(), "\"custom\",\"slow\",,30,3,900,30,30,3.000\n"));
}

void test_global_runner() {
  pb::runner r;  // takes the benchmarks PICOBENCH registered
  std::ostringstream out, err;
  r.set_output_streams(out, err);
  const char* argv[] = {"probe", "--iters=4,8", "--samples=1", "--out-fmt=csv"};
  CHECK(r.parse_cmd_line(4, argv));
  CHECK(r.run(1) == pb::no_error && err.str().empty());
  CHECK(contains(out.str(), "\"global\",\"registered\",*,4,1,20,1,5,1.000\n"));
  CHECK(contains(out.str(), "\"global\",\"registered\",*,8,1,40,1,5,1.000\n"));
}

// Runs a and b in one suite under --compare-results with the default text report.
int compare(pb::benchmark_proc a, pb::benchmark_proc b, std::string& out, std::string& err) {
  pb::local_runner r;
  std::ostringstream o, e;
  r.set_output_streams(o, e);
  r.set_suite("cmp");
  r.add_benchmark("a", a).iterations({3}).samples(2);
  r.add_benchmark("b", b).iterations({3}).samples(2);
  const char* argv[] = {"probe", "--compare-results"};
  const bool parsed = r.parse_cmd_line(2, argv);
  const int rc = parsed ? r.run(1) : -1;
  out = o.str();
  err = e.str();
  return rc;
}

void test_errors() {
  pb::local_runner bad;
  std::ostringstream bad_out, bad_err;
  bad.set_output_streams(bad_out, bad_err);
  const char* bad_argv[] = {"probe", "--samples=x"};
  CHECK(!bad.parse_cmd_line(2, bad_argv) && bad.error() == pb::error_bad_cmd_line_argument);
  CHECK(bad_err.str() == "Error: Bad command-line argument: --samples=x\n");

  std::string out, err;
  CHECK(compare(answer_one, answer_one, out, err) == pb::no_error && err.empty());
  // Rows start a line; "a and b" also appears in a same-function warning.
  CHECK(contains(out, "## cmp:\n") && contains(out, "\n a *") && contains(out, "\n b "));
  CHECK(compare(answer_one, answer_two, out, err) == pb::error_benchmark_compare);
  CHECK(contains(err, "Error: Benchmarks a and b @3 produce different results: 1 and 2\n"));
}

}  // namespace

int main() {
  CHECK(PICOBENCH_VERSION == 21000 && std::strcmp(PICOBENCH_VERSION_STR, "2.10.0") == 0);
  test_local_runner();
  test_global_runner();
  test_errors();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("picobench 2.10.0 ok");
  return 0;
}
