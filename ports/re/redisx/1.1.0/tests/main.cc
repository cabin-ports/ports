// The C API from C++.  redisx.h declares no extern "C", so the include is
// wrapped here, after stddef.h for the size_t that xchange.h uses without
// declaring it.
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <stddef.h>

extern "C" {
#include <redisx.h>
}

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

struct Destroy {
  void operator()(Redis *r) const { redisxDestroy(r); }
  void operator()(RESP *r) const { redisxDestroyRESP(r); }
  void operator()(XField *f) const { xDestroyField(f); }
};

bool hookCalled = false;

void onConnect(Redis *) { hookCalled = true; }

}  // namespace

int main() {
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::fprintf(stderr, "WSAStartup failed\n");
    return 1;
  }
#endif

  {
    std::unique_ptr<Redis, Destroy> r(redisxInit("127.0.0.1"));
    check(r != nullptr, "redisxInit");
    if (!r) {
      return 1;
    }
    check(std::string(r->id) == "127.0.0.1", "id");
    check(redisxInit("") == nullptr, "empty host");
    check(redisxSetUser(r.get(), "probe") == X_SUCCESS, "user");
    check(redisxSetPassword(r.get(), "secret") == X_SUCCESS, "password");
    check(redisxSelectDB(r.get(), 3) == X_SUCCESS, "select db");
    check(redisxSetTcpBuf(r.get(), 65536) == X_SUCCESS, "tcp buffer");
    check(redisxSetReplyTimeout(r.get(), 500) == X_SUCCESS, "reply timeout");
    check(redisxAddConnectHook(r.get(), onConnect) == X_SUCCESS, "hook");
    check(redisxAddConnectHook(r.get(), nullptr) == X_NULL, "null hook");
    check(redisxCheckValid(nullptr) == X_NULL, "null instance");

    // Port 1 on the loopback is closed; the hook runs only once connected.
    check(redisxSetPort(r.get(), 1) == X_SUCCESS, "port");
    check(redisxSetSocketTimeout(r.get(), 500) == X_SUCCESS, "socket timeout");
    check(redisxConnect(r.get(), TRUE) == X_NO_SERVICE, "refused");
    check(!redisxIsConnected(r.get()) && !redisxHasPipeline(r.get()),
          "not connected");
    check(!hookCalled, "hook not called");
  }

  char first[] = "alpha";
  char second[] = "beta";
  RESP a = {RESP_BULK_STRING, 5, first};
  RESP b = {RESP_BULK_STRING, 4, second};
  RESP *items[] = {&a, &b};
  RESP array = {RESP_ARRAY, 2, items};
  check(redisxIsArrayType(&array) && redisxHasComponents(&array), "array");

  std::unique_ptr<RESP, Destroy> copy(redisxCopyOfRESP(&array));
  check(copy != nullptr && copy->type == RESP_ARRAY && copy->n == 2,
        "array copy");
  if (copy) {
    RESP **c = static_cast<RESP **>(copy->value);
    check(c != items && c[0] != &a && redisxIsEqualRESP(c[0], &a) &&
              redisxIsEqualRESP(c[1], &b),
          "deep copy");
    check(std::string(static_cast<char *>(c[1]->value)) == "beta",
          "copied text");
  }
  check(redisxCheckRESP(&array, RESP_ARRAY, 2) == X_SUCCESS, "array size");
  check(redisxCheckRESP(&array, RESP_ARRAY, 3) == REDIS_UNEXPECTED_RESP,
        "wrong array size");

  double pi = 3.25;
  RESP d = {RESP3_DOUBLE, 0, &pi};
  RESP t = {RESP3_BOOLEAN, 1, nullptr};
  std::unique_ptr<XField, Destroy> fd(redisxRESP2XField("d", &d));
  std::unique_ptr<XField, Destroy> ft(redisxRESP2XField("t", &t));
  check(fd && fd->type == X_DOUBLE && xGetAsDouble(fd.get()) == 3.25,
        "double field");
  check(ft && ft->type == X_BOOLEAN && *static_cast<XBoolean *>(ft->value),
        "boolean field");

#ifdef _WIN32
  WSACleanup();
#endif
  if (failures != 0) {
    return 1;
  }
  std::printf("redisx: C++ consumer configured, copied and converted RESP\n");
  return 0;
}
