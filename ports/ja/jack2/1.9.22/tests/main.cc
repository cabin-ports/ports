// Uses the JACK client API from C++ through the weak-linking shim: C
// callbacks from captureless lambdas, then client and port calls that
// must return the shim's fallbacks when no libjack is installed.  It never
// connects to a JACK server.
#include <jack/jack.h>

#include <cstdio>
#include <cstring>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  jack_set_error_function([](const char* msg) {
    std::fprintf(stderr, "jack error: %s\n", msg);
  });
  jack_set_info_function([](const char*) {});
  jack_free(nullptr);

  if (jack_client_name_size() == -1) {
    std::puts("jack2: libjack absent");
    jack_status_t status = JackVersionError;
    check(jack_client_open("cabin-ports-probe-cxx", JackNoStartServer,
                           &status) == nullptr,
          "jack_client_open fallback");
    check(status == JackVersionError, "jack_client_open leaves status");
    check(jack_get_version_string() == nullptr, "version string fallback");
    check(jack_get_client_name(nullptr) == nullptr, "client name fallback");
    check(jack_port_by_name(nullptr, "system:capture_1") == nullptr,
          "port lookup fallback");
    check(jack_get_sample_rate(nullptr) == static_cast<jack_nframes_t>(-1),
          "sample rate fallback");
    check(jack_port_type_size() == -1, "port type size fallback");
  } else {
    std::puts("jack2: libjack present");
    const char* version = jack_get_version_string();
    check(version != nullptr && std::strlen(version) > 0, "version string");
    check(jack_port_type_size() > 1, "port type size");
  }

  if (failures == 0) {
    std::puts("jack2: ok");
  }
  return failures == 0 ? 0 : 1;
}
