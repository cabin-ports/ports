#include <nvtx3/nvtx3.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                       \
    }                                                                 \
  } while (0)

struct my_domain {
  static constexpr char const* name{"my domain"};
};

struct my_message {
  static constexpr char const* message{"registered"};
};

int main() {
  // The attributes are plain data, so they are observable without a tool.
  nvtx3::event_attributes attr{"msg", nvtx3::rgb{255, 128, 1},
                               nvtx3::payload{std::int64_t{-42}},
                               nvtx3::category{7}};
  nvtxEventAttributes_t const& a = *attr.get();
  CHECK(a.version == NVTX_VERSION);
  CHECK(a.size == NVTX_EVENT_ATTRIB_STRUCT_SIZE);
  CHECK(a.category == 7);
  CHECK(a.colorType == NVTX_COLOR_ARGB);
  CHECK(a.color == 0xFFFF8001u);
  CHECK(a.payloadType == NVTX_PAYLOAD_TYPE_INT64);
  CHECK(a.payload.llValue == -42);
  CHECK(a.messageType == NVTX_MESSAGE_TYPE_ASCII);
  CHECK(std::strcmp(a.message.ascii, "msg") == 0);

  nvtx3::event_attributes empty;
  CHECK(empty.get()->colorType == NVTX_COLOR_UNKNOWN);
  CHECK(empty.get()->messageType == NVTX_MESSAGE_UNKNOWN);

  // No tool is injected: a named domain and registered strings are null
  // handles, and scoped ranges, marks and the C pop report no tracking.
  CHECK(static_cast<nvtxDomainHandle_t>(nvtx3::domain::get<my_domain>()) ==
        nullptr);
  auto const& reg = nvtx3::registered_string_in<my_domain>::get<my_message>();
  nvtx3::event_attributes reg_attr{reg};
  CHECK(reg_attr.get()->messageType == NVTX_MESSAGE_TYPE_REGISTERED);
  CHECK(reg_attr.get()->message.registered == nullptr);

  {
    nvtx3::scoped_range r{"scope"};
    nvtx3::scoped_range_in<my_domain> rd{attr};
    nvtx3::mark(attr);
    nvtx3::mark_in<my_domain>(attr);
    CHECK(nvtxRangePushA("range") == NVTX_NO_PUSH_POP_TRACKING);
  }
  CHECK(nvtxRangePop() == NVTX_NO_PUSH_POP_TRACKING);
  return 0;
}
