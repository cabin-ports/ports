// The only header here declaring the core API, so the plugin below compiles
// only through this top-level copy of lv2/core/lv2.h.
#include <lv2.h>

#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/urid/urid.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// A host-side URID map handing out ids from 1.
LV2_URID map_uri(LV2_URID_Map_Handle handle, const char* uri) {
  auto* uris = static_cast<std::vector<std::string>*>(handle);
  for (std::size_t i = 0; i < uris->size(); ++i) {
    if ((*uris)[i] == uri) return static_cast<LV2_URID>(i + 1);
  }
  uris->push_back(uri);
  return static_cast<LV2_URID>(uris->size());
}

int fail(const char* what) {
  std::fprintf(stderr, "lv2: %s\n", what);
  return 1;
}

// Forges the same tuple into buf: a long, a double, a bool, a URID and an
// int vector.
LV2_Atom_Forge_Ref forge_tuple(LV2_Atom_Forge& forge, uint8_t* buf,
                               std::size_t size, LV2_URID urid) {
  static const int32_t samples[4] = {1, -2, 3, -4};
  lv2_atom_forge_set_buffer(&forge, buf, size);
  LV2_Atom_Forge_Frame frame;
  const LV2_Atom_Forge_Ref ref = lv2_atom_forge_tuple(&forge, &frame);
  lv2_atom_forge_long(&forge, INT64_C(1) << 40);
  lv2_atom_forge_double(&forge, 0.25);
  lv2_atom_forge_bool(&forge, true);
  lv2_atom_forge_urid(&forge, urid);
  lv2_atom_forge_vector(&forge, sizeof(int32_t), forge.Int, 4, samples);
  lv2_atom_forge_pop(&forge, &frame);
  return ref;
}

// A plugin behind the core descriptor ABI: out = in * gain.
struct Amp {
  const float* gain = nullptr;
  const float* in = nullptr;
  float* out = nullptr;
};

LV2_Handle amp_instantiate(const LV2_Descriptor*, double, const char*,
                           const LV2_Feature* const*) {
  return new Amp();
}

void amp_connect_port(LV2_Handle instance, uint32_t port, void* data) {
  auto* amp = static_cast<Amp*>(instance);
  if (port == 0) amp->gain = static_cast<const float*>(data);
  if (port == 1) amp->in = static_cast<const float*>(data);
  if (port == 2) amp->out = static_cast<float*>(data);
}

void amp_run(LV2_Handle instance, uint32_t n_samples) {
  const auto* amp = static_cast<const Amp*>(instance);
  for (uint32_t i = 0; i < n_samples; ++i) {
    amp->out[i] = amp->in[i] * *amp->gain;
  }
}

void amp_cleanup(LV2_Handle instance) { delete static_cast<Amp*>(instance); }

const LV2_Descriptor amp_descriptor = {
    "http://example.org/amp", amp_instantiate, amp_connect_port, nullptr,
    amp_run, nullptr, amp_cleanup, nullptr};

}  // namespace

int main() {
  std::vector<std::string> uris;
  LV2_URID_Map map = {&uris, map_uri};

  // Host a plugin through the descriptor, with the map as its one feature.
  {
    const LV2_Feature map_feature = {LV2_URID__map, &map};
    const LV2_Feature* const features[] = {&map_feature, nullptr};
    const LV2_Descriptor& d = amp_descriptor;
    LV2_Handle instance = d.instantiate(&d, 48000.0, "", features);
    if (!instance) return fail("instantiate");
    float gain = 0.5f;
    const float in[3] = {2.0f, -4.0f, 1.0f};
    float out[3] = {};
    d.connect_port(instance, 0, &gain);
    d.connect_port(instance, 1, const_cast<float*>(in));
    d.connect_port(instance, 2, out);
    d.run(instance, 3);
    d.cleanup(instance);
    if (out[0] != 1.0f || out[1] != -2.0f || out[2] != 0.5f)
      return fail("plugin run");
  }

  LV2_Atom_Forge forge;
  lv2_atom_forge_init(&forge, &map);
  const LV2_URID eg_target = map_uri(&uris, "http://example.org/target");

  // Walk a tuple of mixed atoms in order.
  alignas(8) uint8_t buf_a[256];
  const LV2_Atom_Forge_Ref ref_a = forge_tuple(forge, buf_a, sizeof(buf_a),
                                               eg_target);
  if (!ref_a) return fail("forge tuple");
  const auto* tuple = reinterpret_cast<const LV2_Atom_Tuple*>(
      lv2_atom_forge_deref(&forge, ref_a));
  std::vector<LV2_URID> types;
  int64_t long_value = 0;
  double double_value = 0;
  int32_t bool_value = 0;
  LV2_URID urid_value = 0;
  std::vector<int32_t> vector_values;
  LV2_ATOM_TUPLE_FOREACH (tuple, item) {
    types.push_back(item->type);
    if (item->type == forge.Long) {
      long_value = reinterpret_cast<const LV2_Atom_Long*>(item)->body;
    } else if (item->type == forge.Double) {
      double_value = reinterpret_cast<const LV2_Atom_Double*>(item)->body;
    } else if (item->type == forge.Bool) {
      bool_value = reinterpret_cast<const LV2_Atom_Bool*>(item)->body;
    } else if (item->type == forge.URID) {
      urid_value = reinterpret_cast<const LV2_Atom_URID*>(item)->body;
    } else if (item->type == forge.Vector) {
      const auto* vec = reinterpret_cast<const LV2_Atom_Vector*>(item);
      if (vec->body.child_type != forge.Int ||
          vec->body.child_size != sizeof(int32_t))
        return fail("vector header");
      const auto* elems = static_cast<const int32_t*>(
          LV2_ATOM_CONTENTS_CONST(LV2_Atom_Vector, vec));
      const uint32_t n = (vec->atom.size - sizeof(vec->body)) / sizeof(int32_t);
      vector_values.assign(elems, elems + n);
    }
  }
  if (types != std::vector<LV2_URID>{forge.Long, forge.Double, forge.Bool,
                                     forge.URID, forge.Vector})
    return fail("tuple order");
  if (long_value != (INT64_C(1) << 40) || double_value != 0.25 ||
      bool_value != 1 || urid_value != eg_target ||
      vector_values != std::vector<int32_t>{1, -2, 3, -4})
    return fail("tuple values");

  // Equal atoms compare equal byte for byte; a changed value does not.
  alignas(8) uint8_t buf_b[256];
  const LV2_Atom* a = lv2_atom_forge_deref(&forge, ref_a);
  const LV2_Atom* b = lv2_atom_forge_deref(
      &forge, forge_tuple(forge, buf_b, sizeof(buf_b), eg_target));
  if (!lv2_atom_equals(a, b) ||
      lv2_atom_total_size(a) != lv2_atom_total_size(b))
    return fail("equal tuples");
  const LV2_Atom* c = lv2_atom_forge_deref(
      &forge, forge_tuple(forge, buf_b, sizeof(buf_b), eg_target + 1));
  if (lv2_atom_equals(a, c)) return fail("different tuples");

  // Append events to a sequence until its capacity runs out.
  struct {
    LV2_Atom_Event event;
    int32_t value;
  } event = {};
  event.event.body.type = forge.Int;
  event.event.body.size = sizeof(int32_t);
  // Room for exactly two events, each padded to 64 bits in the sequence
  // (sizeof(event) is 20 where int64_t aligns to 4, as on i386).
  constexpr std::size_t padded_event =
      (sizeof(LV2_Atom_Event) + sizeof(int32_t) + 7) / 8 * 8;
  alignas(8) uint8_t seq_buf[sizeof(LV2_Atom_Sequence) + 2 * padded_event];
  auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(seq_buf);
  seq->atom.type = forge.Sequence;
  lv2_atom_sequence_clear(seq);
  const uint32_t capacity = sizeof(seq_buf) - sizeof(LV2_Atom);
  int appended = 0;
  for (int i = 0; i < 3; ++i) {
    event.event.time.frames = 10 * i;
    event.value = 100 + i;
    if (lv2_atom_sequence_append_event(seq, capacity, &event.event)) ++appended;
  }
  if (appended != 2) return fail("sequence capacity");
  std::vector<int64_t> times;
  std::vector<int32_t> values;
  LV2_ATOM_SEQUENCE_FOREACH (seq, ev) {
    times.push_back(ev->time.frames);
    values.push_back(
        *static_cast<const int32_t*>(LV2_ATOM_BODY_CONST(&ev->body)));
  }
  if (times != std::vector<int64_t>{0, 10} ||
      values != std::vector<int32_t>{100, 101})
    return fail("sequence contents");

  std::puts("lv2 consumer ok");
  return 0;
}
