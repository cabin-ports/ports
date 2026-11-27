extern "C" {
#include <chadwick/chadwick.h>
}

#include <cstdio>
#include <cstring>

static int failures;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                            \
    }                                                                        \
  } while (0)

static int parse(const char *text, CWEventData *ev) {
  char buf[64];
  std::strcpy(buf, text);
  return cw_parse_event(buf, ev);
}

int main() {
  CWEventData ev;
  CWGame *game;
  char id[] = "BOS202604010";
  char label[] = "visteam";
  char data[] = "NYA";

  /* Single to left field, batter takes first. */
  CHECK(parse("S7", &ev));
  CHECK(ev.event_type == CW_EVENT_SINGLE);
  CHECK(ev.fielded_by == 7);
  CHECK(cw_event_is_batter(&ev) && cw_event_is_official_ab(&ev));
  CHECK(cw_event_outs_on_play(&ev) == 0 && cw_event_runs_on_play(&ev) == 0);

  /* Ground out 6-3. */
  CHECK(parse("63", &ev));
  CHECK(ev.event_type == CW_EVENT_GENERICOUT);
  CHECK(ev.num_putouts == 1 && ev.putouts[0] == 3);
  CHECK(ev.num_assists == 1 && ev.assists[0] == 6);
  CHECK(cw_event_outs_on_play(&ev) == 1);

  /* Double play with a force out of the runner on first. */
  CHECK(parse("64(1)3/GDP", &ev));
  CHECK(ev.event_type == CW_EVENT_GENERICOUT);
  CHECK(ev.gdp_flag && ev.dp_flag);
  CHECK(cw_event_outs_on_play(&ev) == 2);
  CHECK(cw_event_runner_put_out(&ev, 1));

  /* Strikeout, catcher to first. */
  CHECK(parse("K23", &ev));
  CHECK(ev.event_type == CW_EVENT_STRIKEOUT);
  CHECK(ev.putouts[0] == 3 && ev.assists[0] == 2);
  CHECK(cw_event_outs_on_play(&ev) == 1);

  /* Error on the shortstop. */
  CHECK(parse("E6", &ev));
  CHECK(ev.event_type == CW_EVENT_ERROR);
  CHECK(ev.num_errors == 1 && ev.errors[0] == 6);

  /* Walk with a run scoring from third on a bases-loaded walk. */
  CHECK(parse("W.1-2;2-3;3-H", &ev));
  CHECK(ev.event_type == CW_EVENT_WALK);
  CHECK(cw_event_runs_on_play(&ev) == 1);
  CHECK(!cw_event_is_official_ab(&ev));

  /* Home run with the runner on first scoring too. */
  CHECK(parse("HR/78/F.1-H", &ev));
  CHECK(ev.event_type == CW_EVENT_HOMERUN);
  CHECK(cw_event_runs_on_play(&ev) == 2);
  CHECK(std::strcmp(ev.hit_location, "78") == 0);

  /* Rejected strings. */
  CHECK(!parse("ZZ", &ev));
  CHECK(!parse("S7.", &ev));

  game = cw_game_create(id);
  CHECK(game != 0 && std::strcmp(game->game_id, "BOS202604010") == 0);
  cw_game_info_append(game, label, data);
  CHECK(std::strcmp(cw_game_info_lookup(game, label), "NYA") == 0);
  cw_game_cleanup(game);

  return failures != 0;
}
