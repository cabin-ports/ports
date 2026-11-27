#include <abl_link.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      fprintf(stderr, "abl_link: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

int main(void) {
  /* Nothing here enables networking, so no peers ever appear. */
  struct abl_link link = abl_link_create(120.0);
  abl_link_session_state st = abl_link_create_session_state();

  CHECK(!abl_link_is_enabled(link));
  CHECK(abl_link_num_peers(link) == 0);

  abl_link_capture_app_session_state(link, st);
  CHECK(abl_link_tempo(st) == 120.0);
  CHECK(!abl_link_is_playing(st));

  const int64_t t = abl_link_clock_micros(link);
  abl_link_set_tempo(st, 140.0, t);
  abl_link_force_beat_at_time(st, 0.0, t, 4.0);
  abl_link_set_is_playing(st, true, t);
  abl_link_commit_app_session_state(link, st);

  abl_link_capture_app_session_state(link, st);
  CHECK(abl_link_tempo(st) == 140.0);
  CHECK(abl_link_is_playing(st));
  CHECK(fabs(abl_link_beat_at_time(st, t, 4.0)) < 1e-6);

  /* One beat at 140 bpm is 60e6 / 140 = 428571.43 microseconds. */
  const int64_t one_beat = abl_link_time_at_beat(st, 1.0, 4.0) - t;
  CHECK(one_beat >= 428571 && one_beat <= 428572);
  CHECK(fabs(abl_link_phase_at_time(st, abl_link_time_at_beat(st, 5.0, 4.0), 4.0) -
             1.0) < 1e-3);

  /* The name is applied on Link's thread, so wait for it (at most 5 s). */
  abl_link_audio_set_peer_name(link, "cabin");
  char name[16] = "";
  const int64_t deadline = abl_link_clock_micros(link) + 5000000;
  while (abl_link_audio_peer_name(link, name, sizeof name) != 5 &&
         abl_link_clock_micros(link) < deadline) {
  }
  CHECK(abl_link_audio_peer_name(link, name, sizeof name) == 5);
  CHECK(strcmp(name, "cabin") == 0);
  CHECK(!abl_link_audio_is_link_audio_enabled(link));

  abl_link_destroy_session_state(st);
  abl_link_destroy(link);
  if (failures == 0) puts("abl_link ok");
  return failures != 0;
}
