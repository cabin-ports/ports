/* Calls the JACK client API through the weak-linking shim.  Without an
 * installed libjack, each forwarded call must return the shim's fallback;
 * with one, the real answers.  It never connects to a JACK server. */
#include <jack/jack.h>
#include <jack/midiport.h>

#include <stdio.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main(void) {
  int name_size = jack_client_name_size();
  int port_name_size = jack_port_name_size();
  int port_type_size = jack_port_type_size();
  const char *version = jack_get_version_string();
  int major = -7, minor = -7, micro = -7, proto = -7;

  jack_get_version(&major, &minor, &micro, &proto);

  if (name_size == -1) {
    jack_status_t status = JackVersionError;
    puts("jack2: libjack absent");
    check(port_name_size == -1, "port name size fallback");
    check(port_type_size == -1, "port type size fallback");
    check(version == NULL, "version string fallback");
    check(major == -7 && minor == -7 && micro == -7 && proto == -7,
          "jack_get_version leaves its outputs");
    check(jack_client_open("cabin-ports-probe", JackNoStartServer,
                           &status) == NULL,
          "jack_client_open fallback");
    check(status == JackVersionError, "jack_client_open leaves status");
    check(jack_get_time() == (jack_time_t)-1, "jack_get_time fallback");
    check(jack_midi_get_event_count(NULL) == (jack_nframes_t)-1,
          "MIDI event count fallback");
    /* Declared JACK_WEAK_EXPORT, so this reaches the shim through a weak
     * reference on non-Windows GCC and Clang. */
    check(jack_port_type_get_buffer_size(NULL, JACK_DEFAULT_AUDIO_TYPE) ==
              (size_t)-1,
          "weak-declared call reaches the shim");
  } else {
    puts("jack2: libjack present");
    check(name_size > 1, "client name size");
    /* A full port name is "client:port". */
    check(port_name_size > name_size, "port name size");
    check(port_type_size > 1, "port type size");
    check(version != NULL && version[0] != '\0', "version string");
  }

  if (failures == 0) {
    puts("jack2: ok");
  }
  return failures == 0 ? 0 : 1;
}
