#include <dap/io.h>
#include <dap/protocol.h>
#include <dap/session.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <future>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  // optional and any, the value types every protocol message is built from.
  dap::optional<int> o;
  check(!o.has_value(), "empty optional");
  check(o.value(7) == 7, "optional default value");
  o = 3;
  check(o.has_value() && o.value() == 3, "optional holds 3");

  dap::any a = dap::integer(5);
  check(a.is<dap::integer>(), "any holds an integer");
  check(!a.is<dap::string>(), "any does not hold a string");
  check(int64_t(a.get<dap::integer>()) == 5, "any integer is 5");
  a = dap::string("five");
  check(a.is<dap::string>() && a.get<dap::string>() == "five",
        "any holds the string five");

  // State the handlers capture outlives the sessions, whose destructors join
  // the threads that run the handlers.
  dap::InitializeRequest seen;
  std::promise<std::string> output;

  // A client and a server session over two in-memory pipes; every message
  // is serialized to JSON and parsed back on the receiving session's thread.
  auto client = dap::Session::create();
  auto server = dap::Session::create();

  server->registerHandler([&](const dap::InitializeRequest& req) {
    seen = req;
    dap::InitializeResponse res;
    res.supportsConfigurationDoneRequest = true;
    res.supportsFunctionBreakpoints = false;
    res.completionTriggerCharacters = dap::array<dap::string>{".", "->"};
    return res;
  });
  server->registerHandler([](const dap::ThreadsRequest&) {
    return dap::Error("no threads: %d", 42);
  });

  client->registerHandler([&](const dap::OutputEvent& event) {
    output.set_value(event.category.value("") + ":" + event.output);
  });

  auto client2server = dap::pipe();
  auto server2client = dap::pipe();
  client->bind(server2client, client2server);
  server->bind(client2server, server2client);

  const auto timeout = std::chrono::seconds(10);

  dap::InitializeRequest req;
  req.adapterID = "cabin";
  req.clientName = "probe";
  req.linesStartAt1 = true;
  req.columnsStartAt1 = false;
  auto initialize = client->send(req);
  if (initialize.wait_for(timeout) != dap::future_status::ready) {
    std::fprintf(stderr, "FAIL: no initialize response\n");
    return 1;
  }
  auto init = initialize.get();
  check(!init.error, "initialize succeeds");
  check(seen.adapterID == "cabin", "server sees adapterID");
  check(seen.clientName.value("") == "probe", "server sees clientName");
  check(seen.linesStartAt1.has_value() && bool(seen.linesStartAt1.value()),
        "server sees linesStartAt1 = true");
  check(seen.columnsStartAt1.has_value() && !seen.columnsStartAt1.value(),
        "server sees columnsStartAt1 = false");
  check(!seen.locale.has_value(), "unset locale stays unset");
  check(init.response.supportsConfigurationDoneRequest.has_value() &&
            bool(init.response.supportsConfigurationDoneRequest.value()),
        "client sees supportsConfigurationDoneRequest = true");
  check(init.response.supportsFunctionBreakpoints.has_value() &&
            !init.response.supportsFunctionBreakpoints.value(),
        "client sees supportsFunctionBreakpoints = false");
  check(!init.response.supportsStepBack.has_value(),
        "unset supportsStepBack stays unset");
  check(init.response.completionTriggerCharacters.has_value() &&
            init.response.completionTriggerCharacters.value() ==
                dap::array<dap::string>{".", "->"},
        "client sees completionTriggerCharacters");

  auto threads = client->send(dap::ThreadsRequest{});
  if (threads.wait_for(timeout) != dap::future_status::ready) {
    std::fprintf(stderr, "FAIL: no threads response\n");
    return 1;
  }
  auto thr = threads.get();
  check(bool(thr.error), "threads request fails");
  check(thr.error.message == "no threads: 42", "error message round trip");

  dap::OutputEvent event;
  event.category = "console";
  event.output = "hello\n";
  server->send(event);
  auto got = output.get_future();
  if (got.wait_for(timeout) != std::future_status::ready) {
    std::fprintf(stderr, "FAIL: no output event\n");
    return 1;
  }
  check(got.get() == "console:hello\n", "output event round trip");

  if (failures != 0) {
    return 1;
  }
  std::puts("cppdap ok");
  return 0;
}
