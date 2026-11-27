/* A host walks a locally defined plugin, a stereo gain with one parameter,
 * through the entry, the factory, the descriptor, two extensions and
 * process(); the checks are on the structs and values clap.h defines. */
#include <clap/clap.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

enum { GAIN_ID = 7 };

static double gain = 1.0;
static int notes_on = 0;
static int inits = 0;
static int destroyed = 0;

static const char *const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, NULL};

static const clap_plugin_descriptor_t desc = {
    CLAP_VERSION_INIT, "org.example.gain", "Gain", "Example", "https://example.org", "", "", "1.0", "A gain", features};

static uint32_t ports_count(const clap_plugin_t *p, bool is_input) {
    (void)p;
    (void)is_input;
    return 1;
}

static bool ports_get(const clap_plugin_t *p, uint32_t i, bool is_input, clap_audio_port_info_t *info) {
    (void)p;
    if (i != 0) return false;
    memset(info, 0, sizeof *info);
    info->id = is_input ? 0 : 1;
    snprintf(info->name, sizeof info->name, "%s", is_input ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t ports_ext = {ports_count, ports_get};

static uint32_t params_count(const clap_plugin_t *p) {
    (void)p;
    return 1;
}

static bool params_get_info(const clap_plugin_t *p, uint32_t i, clap_param_info_t *info) {
    (void)p;
    if (i != 0) return false;
    memset(info, 0, sizeof *info);
    info->id = GAIN_ID;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    snprintf(info->name, sizeof info->name, "%s", "Gain");
    info->min_value = 0.0;
    info->max_value = 4.0;
    info->default_value = 1.0;
    return true;
}

static bool params_get_value(const clap_plugin_t *p, clap_id id, double *out) {
    (void)p;
    if (id != GAIN_ID) return false;
    *out = gain;
    return true;
}

static bool params_value_to_text(const clap_plugin_t *p, clap_id id, double v, char *buf, uint32_t cap) {
    (void)p;
    if (id != GAIN_ID) return false;
    snprintf(buf, cap, "%.2fx", v);
    return true;
}

static bool params_text_to_value(const clap_plugin_t *p, clap_id id, const char *text, double *out) {
    (void)p;
    if (id != GAIN_ID) return false;
    return sscanf(text, "%lf", out) == 1;
}

static void params_flush(const clap_plugin_t *p, const clap_input_events_t *in, const clap_output_events_t *out) {
    (void)p;
    (void)out;
    uint32_t i;
    for (i = 0; i < in->size(in); ++i) {
        const clap_event_header_t *h = in->get(in, i);
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (h->type == CLAP_EVENT_PARAM_VALUE) {
            const clap_event_param_value_t *e = (const clap_event_param_value_t *)h;
            if (e->param_id == GAIN_ID) gain = e->value;
        } else if (h->type == CLAP_EVENT_NOTE_ON) {
            ++notes_on;
        }
    }
}

static const clap_plugin_params_t params_ext = {params_count,         params_get_info,     params_get_value,
                                                params_value_to_text, params_text_to_value, params_flush};

static bool plugin_init(const clap_plugin_t *p) {
    (void)p;
    ++inits;
    return true;
}

static void plugin_destroy(const clap_plugin_t *p) {
    (void)p;
    ++destroyed;
}

static bool plugin_activate(const clap_plugin_t *p, double rate, uint32_t min, uint32_t max) {
    (void)p;
    return rate == 48000.0 && min == 1 && max == 64;
}

static void plugin_deactivate(const clap_plugin_t *p) { (void)p; }
static bool plugin_start(const clap_plugin_t *p) {
    (void)p;
    return true;
}
static void plugin_stop(const clap_plugin_t *p) { (void)p; }
static void plugin_reset(const clap_plugin_t *p) { (void)p; }

static clap_process_status plugin_process(const clap_plugin_t *p, const clap_process_t *proc) {
    uint32_t c, n;
    params_flush(p, proc->in_events, proc->out_events);
    if (proc->audio_inputs_count != 1 || proc->audio_outputs_count != 1) return CLAP_PROCESS_ERROR;
    for (c = 0; c < proc->audio_inputs[0].channel_count; ++c)
        for (n = 0; n < proc->frames_count; ++n)
            proc->audio_outputs[0].data32[c][n] = (float)(proc->audio_inputs[0].data32[c][n] * gain);
    return CLAP_PROCESS_CONTINUE;
}

static const void *plugin_get_extension(const clap_plugin_t *p, const char *id) {
    (void)p;
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &ports_ext;
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) return &params_ext;
    return NULL;
}

static void plugin_on_main_thread(const clap_plugin_t *p) { (void)p; }

static const clap_plugin_t plugin = {&desc,          NULL,          plugin_init,         plugin_destroy,
                                     plugin_activate, plugin_deactivate, plugin_start,   plugin_stop,
                                     plugin_reset,    plugin_process,    plugin_get_extension, plugin_on_main_thread};

static uint32_t factory_count(const clap_plugin_factory_t *f) {
    (void)f;
    return 1;
}

static const clap_plugin_descriptor_t *factory_descriptor(const clap_plugin_factory_t *f, uint32_t i) {
    (void)f;
    return i == 0 ? &desc : NULL;
}

static const clap_plugin_t *factory_create(const clap_plugin_factory_t *f, const clap_host_t *host,
                                           const char *id) {
    (void)f;
    (void)host;
    return strcmp(id, desc.id) == 0 ? &plugin : NULL;
}

static const clap_plugin_factory_t factory = {factory_count, factory_descriptor, factory_create};

static bool entry_init(const char *path) { return path != NULL; }
static void entry_deinit(void) {}

static const void *entry_get_factory(const char *id) {
    return strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : NULL;
}

static const clap_plugin_entry_t entry = {CLAP_VERSION_INIT, entry_init, entry_deinit, entry_get_factory};

/* The host side. */
static const clap_event_header_t *events[3];

static uint32_t events_size(const clap_input_events_t *l) {
    (void)l;
    return 3;
}

static const clap_event_header_t *events_get(const clap_input_events_t *l, uint32_t i) {
    (void)l;
    return events[i];
}

static bool events_try_push(const clap_output_events_t *l, const clap_event_header_t *e) {
    (void)l;
    (void)e;
    return false;
}

static const void *host_get_extension(const clap_host_t *h, const char *id) {
    (void)h;
    (void)id;
    return NULL;
}
static void host_request(const clap_host_t *h) { (void)h; }

int main(void) {
    const clap_version_t v = CLAP_VERSION;
    const clap_version_t dev = {0, 9, 0};
    CHECK(v.major == 1 && v.minor == 2 && v.revision == 10);
    CHECK(clap_version_is_compatible(v));
    CHECK(!clap_version_is_compatible(dev));
    CHECK(CLAP_VERSION_EQ(1, 2, 10) && CLAP_VERSION_GE(1, 2, 9) && CLAP_VERSION_LT(1, 3, 0));
    CHECK(!CLAP_VERSION_LT(1, 2, 10) && !CLAP_VERSION_GE(1, 2, 11));

    CHECK(strcmp(CLAP_EXT_PARAMS, "clap.params") == 0);
    CHECK(strcmp(CLAP_EXT_AUDIO_PORTS, "clap.audio-ports") == 0);
    CHECK(strcmp(CLAP_EXT_STATE, "clap.state") == 0);
    CHECK(strcmp(CLAP_EXT_GUI, "clap.gui") == 0);
    CHECK(strcmp(CLAP_PLUGIN_FACTORY_ID, "clap.plugin-factory") == 0);
    CHECK(strcmp(CLAP_PLUGIN_FEATURE_INSTRUMENT, "instrument") == 0);
    CHECK(strcmp(CLAP_PORT_STEREO, "stereo") == 0 && CLAP_AUDIO_PORT_IS_MAIN == 1);
    CHECK(CLAP_INVALID_ID == 0xFFFFFFFFu && CLAP_NAME_SIZE == 256 && CLAP_PATH_SIZE == 1024);
    CHECK(CLAP_CORE_EVENT_SPACE_ID == 0 && CLAP_EVENT_NOTE_ON == 0 && CLAP_EVENT_PARAM_VALUE == 5 && CLAP_EVENT_MIDI2 == 12);

    {
        const clap_host_t host = {CLAP_VERSION_INIT, NULL, "Host", "Vendor", "", "1.0", host_get_extension,
                                  host_request,      host_request, host_request};
        float in0[4] = {1.0f, 2.0f, 3.0f, 4.0f}, in1[4] = {-1.0f, -2.0f, -3.0f, -4.0f};
        float out0[4] = {0}, out1[4] = {0};
        float *in_data[2] = {in0, in1}, *out_data[2] = {out0, out1};
        clap_audio_buffer_t in_buf = {in_data, NULL, 2, 0}, out_buf = {out_data, NULL, 2, 0};
        clap_event_note_t note;
        clap_event_param_value_t pv;
        clap_event_param_value_t other;
        clap_input_events_t in_events = {NULL, events_size, events_get};
        clap_output_events_t out_events = {NULL, events_try_push};
        clap_process_t proc;
        clap_audio_port_info_t port;
        clap_param_info_t info;
        char text[32];
        double value = 0;

        const clap_plugin_entry_t *e = &entry;
        const clap_plugin_factory_t *f;
        const clap_plugin_descriptor_t *d;
        const clap_plugin_t *p;
        const clap_plugin_audio_ports_t *ap;
        const clap_plugin_params_t *pp;

        CHECK(clap_version_is_compatible(e->clap_version));
        CHECK(e->init("/plugin.clap"));
        f = (const clap_plugin_factory_t *)e->get_factory(CLAP_PLUGIN_FACTORY_ID);
        CHECK(f != NULL && e->get_factory("clap.nothing") == NULL);
        CHECK(f->get_plugin_count(f) == 1);
        d = f->get_plugin_descriptor(f, 0);
        CHECK(d != NULL && f->get_plugin_descriptor(f, 1) == NULL);
        CHECK(strcmp(d->id, "org.example.gain") == 0 && strcmp(d->name, "Gain") == 0);
        CHECK(strcmp(d->features[0], "audio-effect") == 0 && strcmp(d->features[1], "stereo") == 0 &&
              d->features[2] == NULL);

        CHECK(f->create_plugin(f, &host, "org.example.other") == NULL);
        p = f->create_plugin(f, &host, d->id);
        CHECK(p != NULL && p->desc == d && p->init(p) && inits == 1);

        ap = (const clap_plugin_audio_ports_t *)p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
        pp = (const clap_plugin_params_t *)p->get_extension(p, CLAP_EXT_PARAMS);
        CHECK(ap != NULL && pp != NULL && p->get_extension(p, CLAP_EXT_STATE) == NULL);
        CHECK(ap->count(p, true) == 1 && ap->get(p, 0, false, &port));
        CHECK(strcmp(port.name, "Out") == 0 && port.id == 1 && port.channel_count == 2);
        CHECK((port.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0 && strcmp(port.port_type, CLAP_PORT_STEREO) == 0);
        CHECK(port.in_place_pair == CLAP_INVALID_ID);
        CHECK(pp->count(p) == 1 && pp->get_info(p, 0, &info) && !pp->get_info(p, 1, &info));
        CHECK(info.id == GAIN_ID && strcmp(info.name, "Gain") == 0 && info.max_value == 4.0);
        CHECK((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0 && (info.flags & CLAP_PARAM_IS_STEPPED) == 0);
        CHECK(pp->value_to_text(p, GAIN_ID, 2.5, text, sizeof text) && strcmp(text, "2.50x") == 0);
        CHECK(pp->text_to_value(p, GAIN_ID, "0.5", &value) && value == 0.5);

        CHECK(!p->activate(p, 44100.0, 1, 64) && p->activate(p, 48000.0, 1, 64));
        CHECK(p->start_processing(p));

        memset(&note, 0, sizeof note);
        note.header.size = sizeof note;
        note.header.time = 0;
        note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        note.header.type = CLAP_EVENT_NOTE_ON;
        note.key = 60;
        note.velocity = 0.8;
        memset(&pv, 0, sizeof pv);
        pv.header.size = sizeof pv;
        pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        pv.header.type = CLAP_EVENT_PARAM_VALUE;
        pv.param_id = GAIN_ID;
        pv.value = 0.5;
        other = pv;
        other.header.space_id = 1; /* another event space: ignored */
        other.value = 3.0;
        events[0] = &note.header;
        events[1] = &other.header;
        events[2] = &pv.header;

        memset(&proc, 0, sizeof proc);
        proc.steady_time = -1;
        proc.frames_count = 4;
        proc.audio_inputs = &in_buf;
        proc.audio_outputs = &out_buf;
        proc.audio_inputs_count = 1;
        proc.audio_outputs_count = 1;
        proc.in_events = &in_events;
        proc.out_events = &out_events;
        CHECK(p->process(p, &proc) == CLAP_PROCESS_CONTINUE);
        CHECK(notes_on == 1 && pp->get_value(p, GAIN_ID, &value) && value == 0.5);
        CHECK(out0[0] == 0.5f && out0[3] == 2.0f && out1[0] == -0.5f && out1[3] == -2.0f);

        p->stop_processing(p);
        p->deactivate(p);
        p->destroy(p);
        CHECK(destroyed == 1);
        e->deinit();
    }

    if (failures == 0) printf("ok\n");
    return failures != 0;
}
