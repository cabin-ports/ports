/* RedisX without a server: a Redis instance's configuration, the RESP
   helpers, and a connection that a closed local port refuses. */
#ifdef _WIN32
/* Before xchange.h's <windows.h>; the caller starts Winsock. */
#include <winsock2.h>
#elif defined(__linux__)
/* redisx.h declares redisxDeleteEntries from POSIX.1-2001 on. */
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* After stdio.h: xchange.h uses size_t without declaring it. */
#include <redisx.h>

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond);    \
      failures++;                                                         \
    }                                                                     \
  } while (0)

int main(void) {
  Redis *r;
  RESP num = {RESP_INT, 6379, NULL};
  RESP role = {RESP_SIMPLE_STRING, 4, "role"};
  RESP master = {RESP_SIMPLE_STRING, 6, "master"};
  RESP port = {RESP_SIMPLE_STRING, 4, "port"};
  RedisMap entries[2];
  RESP map = {RESP3_MAP, 2, NULL};
  RESP moved = {RESP_ERROR, 25, "MOVED 3999 127.0.0.1:6381"};
  RESP ask = {RESP_ERROR, 23, "ASK 3999 127.0.0.1:6381"};
  RESP *copy;
  RedisMap *e;
  XField *f;
  XStructure *s;
  char *rest = NULL;
#ifdef _WIN32
  WSADATA wsa;

  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    fprintf(stderr, "WSAStartup failed\n");
    return 1;
  }
#endif

  CHECK(strcmp(REDISX_VERSION_STRING, "1.1.0") == 0);

  CHECK(redisxInit(NULL) == NULL);
  r = redisxInit("127.0.0.2");
  CHECK(r != NULL);
  if (r == NULL) {
    return 1;
  }
  CHECK(strcmp(r->id, "127.0.0.2") == 0);
  CHECK(redisxCheckValid(r) == X_SUCCESS);
  CHECK(redisxIsConnected(r) == FALSE);
  CHECK(redisxHasPipeline(r) == FALSE);
  CHECK(redisxSetHostname(r, "") == X_NULL);
  CHECK(redisxSetHostname(r, "127.0.0.1") == X_SUCCESS);
  CHECK(strcmp(r->id, "127.0.0.1") == 0);
  CHECK(redisxGetProtocol(r) == REDISX_RESP2);
  CHECK(redisxSetProtocol(r, REDISX_RESP3) == X_SUCCESS);
  CHECK(redisxGetProtocol(r) == REDISX_RESP3);
  CHECK(redisxSetScanCount(r, 250) == X_SUCCESS);
  CHECK(redisxGetScanCount(r) == 250);
  /* The default build has no TLS. */
  CHECK(redisxSetTLS(r, NULL, NULL) == X_FAILURE);

  printf("redisx: %s configured\n", r->id);
  fflush(stdout);

  /* Port 1 on the loopback is closed. */
  CHECK(redisxSetPort(r, 1) == X_SUCCESS);
  CHECK(redisxSetSocketTimeout(r, 500) == X_SUCCESS);
#ifndef _WIN32
  /* Not on Windows: see the Windows limitation in cabin.toml. */
  CHECK(redisxConnect(r, FALSE) == X_NO_SERVICE);
  CHECK(redisxIsConnected(r) == FALSE);
#endif
#ifdef __linux__
  /* Only built with _POSIX_C_SOURCE >= 200112L (cabin.toml's define). */
  CHECK(redisxDeleteEntries(r, NULL) == X_NULL);
#endif
  redisxDestroy(r);

  CHECK(redisxIsScalarType(&num) == TRUE);
  CHECK(redisxIsStringType(&num) == FALSE);
  CHECK(redisxCheckRESP(&num, RESP_INT, 0) == X_SUCCESS);
  CHECK(redisxCheckRESP(&num, RESP_SIMPLE_STRING, 0) ==
        REDIS_UNEXPECTED_RESP);
  CHECK(redisxCheckRESP(NULL, RESP_INT, 0) == X_NULL);
  CHECK(redisxCheckRESP(&master, RESP_SIMPLE_STRING, 6) == X_SUCCESS);
  CHECK(redisxCheckRESP(&master, RESP_SIMPLE_STRING, 5) ==
        REDIS_UNEXPECTED_RESP);

  CHECK(redisxClusterMoved(&moved) == TRUE);
  CHECK(redisxClusterIsMigrating(&moved) == FALSE);
  CHECK(redisxCheckRESP(&moved, RESP_ERROR, 0) == REDIS_MOVED);
  CHECK(redisxClusterMoved(&ask) == FALSE);
  CHECK(redisxClusterIsRedirected(&ask) == TRUE);
  CHECK(redisxCheckRESP(&ask, RESP_ERROR, 0) == REDIS_MIGRATING);

  copy = redisxCopyOfRESP(&moved);
  CHECK(copy != NULL && copy->value != moved.value);
  CHECK(redisxIsEqualRESP(&moved, copy) == TRUE);
  /* Splits off the error code in place. */
  CHECK(redisxSplitText(copy, &rest) >= 0);
  CHECK(strcmp((char *)copy->value, "MOVED") == 0);
  CHECK(rest != NULL && strcmp(rest, "3999 127.0.0.1:6381") == 0);
  CHECK(redisxIsEqualRESP(&moved, copy) == FALSE);
  redisxDestroyRESP(copy);
  CHECK(redisxSplitText(&master, NULL) == REDIS_UNEXPECTED_RESP);

  entries[0].key = &role;
  entries[0].value = &master;
  entries[1].key = &port;
  entries[1].value = &num;
  map.value = entries;
  CHECK(redisxIsMapType(&map) == TRUE);
  CHECK(redisxIsArrayType(&map) == FALSE);
  e = redisxGetKeywordEntry(&map, "port");
  CHECK(e != NULL && e->value == &num);
  CHECK(redisxGetKeywordEntry(&map, "host") == NULL);
  e = redisxGetMapEntry(&map, &role);
  CHECK(e != NULL && e->value == &master);

  f = redisxRESP2XField("info", &map);
  CHECK(f != NULL && f->type == X_STRUCT);
  if (f != NULL && f->type == X_STRUCT) {
    s = (XStructure *)f->value;
    CHECK(xCountFields(s) == 2);
    CHECK(strcmp(xGetStringValue(xGetField(s, "role")), "master") == 0);
    CHECK(xGetAsLong(xGetField(s, "port"), -1) == 6379);
  }
  xDestroyField(f);

  CHECK(redisxIsGlobPattern("user:*") == TRUE);
  CHECK(redisxIsGlobPattern("user:42") == FALSE);
  CHECK(strcmp(redisxErrorDescription(REDIS_UNEXPECTED_RESP),
               "unexpected Redis response type") == 0);

#ifdef _WIN32
  WSACleanup();
#endif
  if (failures != 0) {
    return 1;
  }
  printf("redisx: configuration, RESP helpers and a refused connection\n");
  return 0;
}
