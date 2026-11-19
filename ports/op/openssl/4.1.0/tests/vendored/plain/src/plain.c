#include "plain.h"

#include <openssl/opensslv.h>

const char *plain_headers(void) { return OPENSSL_VERSION_STR; }
