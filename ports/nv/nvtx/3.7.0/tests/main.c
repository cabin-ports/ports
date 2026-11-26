#include <nvtx3/nvToolsExt.h>

#include <stdio.h>

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                             \
    }                                                       \
  } while (0)

int main(void) {
  nvtxDomainHandle_t domain;
  nvtxEventAttributes_t attr = {0};

  /* No tool is injected, so push/pop are untracked and handles are null. */
  CHECK(nvtxRangePushA("range") == NVTX_NO_PUSH_POP_TRACKING);
  CHECK(nvtxRangePop() == NVTX_NO_PUSH_POP_TRACKING);
  CHECK(NVTX_NO_PUSH_POP_TRACKING == -2);
  CHECK(nvtxRangeStartA("id") == 0);
  nvtxRangeEnd(0);

  attr.version = NVTX_VERSION;
  attr.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
  attr.messageType = NVTX_MESSAGE_TYPE_ASCII;
  attr.message.ascii = "ex";
  CHECK(nvtxRangePushEx(&attr) == NVTX_NO_PUSH_POP_TRACKING);

  nvtxMarkA("mark");
  nvtxMarkEx(&attr);

  domain = nvtxDomainCreateA("domain");
  CHECK(domain == NULL);
  CHECK(nvtxDomainRegisterStringA(domain, "str") == NULL);
  CHECK(nvtxDomainRangePushEx(domain, &attr) == NVTX_NO_PUSH_POP_TRACKING);
  CHECK(nvtxDomainRangePop(domain) == NVTX_NO_PUSH_POP_TRACKING);
  nvtxDomainDestroy(domain);
  return 0;
}
