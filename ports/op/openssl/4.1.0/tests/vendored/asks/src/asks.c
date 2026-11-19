#include "asks.h"

#include <openssl/opensslv.h>

const char *asks_headers(void) { return OPENSSL_VERSION_STR; }
