#include <openssl/crypto.h>

int main(void) { return OPENSSL_version_major() == 0; }
