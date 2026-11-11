#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <lv2/log/log.h>
#include <lv2/log/logger.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The host side: a URID map handing out ids from 1, and a log that keeps
   the last message. */
typedef struct {
  const char* uris[32];
  uint32_t n;
} UriTable;

static LV2_URID map_uri(LV2_URID_Map_Handle handle, const char* uri) {
  UriTable* table = (UriTable*)handle;
  for (uint32_t i = 0; i < table->n; ++i) {
    if (!strcmp(table->uris[i], uri)) return i + 1;
  }
  if (table->n == 32) return 0;
  table->uris[table->n] = uri; /* every URI here is a string literal */
  return ++table->n;
}

typedef struct {
  LV2_URID type;
  char text[64];
} LastMessage;

static int log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char* fmt,
                       va_list ap) {
  LastMessage* last = (LastMessage*)handle;
  last->type = type;
  return vsnprintf(last->text, sizeof(last->text), fmt, ap);
}

static int log_printf(LV2_Log_Handle handle, LV2_URID type, const char* fmt,
                      ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = log_vprintf(handle, type, fmt, ap);
  va_end(ap);
  return n;
}

static int fail(const char* what) {
  fprintf(stderr, "lv2: %s\n", what);
  return 1;
}

int main(void) {
  UriTable table = {{NULL}, 0};
  LV2_URID_Map map = {&table, map_uri};
  LastMessage last = {0, ""};
  LV2_Log_Log log = {&last, log_printf, log_vprintf};

  /* Feature discovery, as a plugin's instantiate does it. */
  const LV2_Feature map_feature = {LV2_URID__map, &map};
  const LV2_Feature log_feature = {LV2_LOG__log, &log};
  const LV2_Feature* const features[] = {&map_feature, &log_feature, NULL};
  void* map_data = NULL;
  void* log_data = NULL;
  const char* missing =
      lv2_features_query(features, LV2_URID__map, &map_data, true,
                         LV2_LOG__log, &log_data, false, NULL);
  if (missing || map_data != &map || log_data != &log) return fail("query");
  LV2_URID_Map* host_map = (LV2_URID_Map*)map_data;
  LV2_Log_Log* host_log = (LV2_Log_Log*)log_data;
  void* schedule = NULL;
  missing = lv2_features_query(features, LV2_WORKER__schedule, &schedule, true,
                               NULL);
  if (!missing || strcmp(missing, LV2_WORKER__schedule) || schedule)
    return fail("missing required feature");

  /* The logger maps its message classes and formats through the host. */
  LV2_Log_Logger logger;
  lv2_log_logger_init(&logger, host_map, host_log);
  lv2_log_warning(&logger, "gain %d%%\n", 150);
  if (last.type != map_uri(&table, LV2_LOG__Warning) ||
      strcmp(last.text, "gain 150%\n"))
    return fail("logger");

  /* Forge an object and read its properties back. */
  const LV2_URID eg_Settings = map_uri(&table, "http://example.org/Settings");
  const LV2_URID eg_gain = map_uri(&table, "http://example.org/gain");
  const LV2_URID eg_channels = map_uri(&table, "http://example.org/channels");
  const LV2_URID eg_name = map_uri(&table, "http://example.org/name");
  LV2_Atom_Forge forge;
  lv2_atom_forge_init(&forge, host_map);
  /* Atoms must be 64-bit aligned; heap storage is, and holds them as the
     types the forge writes. */
  uint8_t* buf = (uint8_t*)malloc(256);
  if (!buf) return fail("malloc");
  lv2_atom_forge_set_buffer(&forge, buf, 256);
  LV2_Atom_Forge_Frame frame;
  const LV2_Atom_Forge_Ref object_ref =
      lv2_atom_forge_object(&forge, &frame, 0, eg_Settings);
  lv2_atom_forge_key(&forge, eg_gain);
  lv2_atom_forge_float(&forge, 0.5f);
  lv2_atom_forge_key(&forge, eg_channels);
  lv2_atom_forge_int(&forge, 2);
  lv2_atom_forge_key(&forge, eg_name);
  lv2_atom_forge_string(&forge, "main", 4);
  lv2_atom_forge_pop(&forge, &frame);
  if (!object_ref) return fail("forge object");

  const LV2_Atom_Object* object =
      (const LV2_Atom_Object*)lv2_atom_forge_deref(&forge, object_ref);
  if (object->atom.type != forge.Object || object->body.otype != eg_Settings)
    return fail("object header");
  const LV2_Atom* gain = NULL;
  const LV2_Atom* channels = NULL;
  const LV2_Atom* name = NULL;
  if (lv2_atom_object_get(object, eg_gain, &gain, eg_channels, &channels,
                          eg_name, &name, 0) != 3)
    return fail("object get");
  if (gain->type != forge.Float || ((const LV2_Atom_Float*)gain)->body != 0.5f)
    return fail("gain");
  if (channels->type != forge.Int || ((const LV2_Atom_Int*)channels)->body != 2)
    return fail("channels");
  if (name->type != forge.String || name->size != 5 ||
      strcmp((const char*)LV2_ATOM_BODY_CONST(name), "main"))
    return fail("name");

  /* A sequence of timestamped MIDI events, iterated in order. */
  const LV2_URID midi_Event = map_uri(&table, LV2_MIDI__MidiEvent);
  const uint8_t note_on[3] = {0x92, 60, 100};
  const uint8_t note_off[3] = {0x82, 60, 0};
  lv2_atom_forge_set_buffer(&forge, buf, 256);
  const LV2_Atom_Forge_Ref sequence_ref =
      lv2_atom_forge_sequence_head(&forge, &frame, 0);
  lv2_atom_forge_frame_time(&forge, 0);
  lv2_atom_forge_atom(&forge, sizeof(note_on), midi_Event);
  lv2_atom_forge_write(&forge, note_on, sizeof(note_on));
  lv2_atom_forge_frame_time(&forge, 64);
  lv2_atom_forge_atom(&forge, sizeof(note_off), midi_Event);
  lv2_atom_forge_write(&forge, note_off, sizeof(note_off));
  lv2_atom_forge_pop(&forge, &frame);

  const LV2_Atom_Sequence* sequence =
      (const LV2_Atom_Sequence*)lv2_atom_forge_deref(&forge, sequence_ref);
  int64_t frames[2] = {-1, -1};
  LV2_Midi_Message_Type types[2] = {LV2_MIDI_MSG_INVALID,
                                    LV2_MIDI_MSG_INVALID};
  int n_events = 0;
  LV2_ATOM_SEQUENCE_FOREACH (sequence, ev) {
    if (n_events == 2 || ev->body.type != midi_Event) return fail("event");
    const uint8_t* msg = (const uint8_t*)LV2_ATOM_BODY_CONST(&ev->body);
    if (!lv2_midi_is_voice_message(msg)) return fail("voice message");
    frames[n_events] = ev->time.frames;
    types[n_events] = lv2_midi_message_type(msg);
    ++n_events;
  }
  if (n_events != 2 || frames[0] != 0 || frames[1] != 64 ||
      types[0] != LV2_MIDI_MSG_NOTE_ON || types[1] != LV2_MIDI_MSG_NOTE_OFF)
    return fail("sequence");

  /* The forge refuses to write past the end of its buffer. */
  lv2_atom_forge_set_buffer(&forge, buf, 8);
  if (lv2_atom_forge_int(&forge, 7)) return fail("overflow");
  free(buf);

  puts("lv2 consumer ok");
  return 0;
}
