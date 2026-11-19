/* The library that runs against the headers it was compiled with, a
 * SHA-256 known answer through libcrypto, and through libssl a TLS 1.3
 * handshake and one message between a client and a server in this process,
 * with a key and a certificate made here. */
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <stdio.h>
#include <string.h>

/* Whether the library that runs is the headers' release or a later one of
 * its series: another program's older copy can come first on PATH. */
static int runs_what_the_headers_describe(void) {
    return OPENSSL_version_major() == OPENSSL_VERSION_MAJOR &&
           OpenSSL_version_num() >= OPENSSL_VERSION_NUMBER;
}

static int sha256_answers(void) {
    static const unsigned char expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    return EVP_Digest("abc", 3, digest, &length, EVP_sha256(), NULL) == 1 && length == 32 &&
           memcmp(digest, expected, 32) == 0;
}

/* A certificate for `localhost` that `key` signs for itself. */
static X509 *self_signed(EVP_PKEY *key) {
    X509 *cert = X509_new();
    X509_NAME *name = X509_NAME_new();
    int ok = cert && name && X509_set_version(cert, 2) &&
             ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) &&
             X509_gmtime_adj(X509_getm_notBefore(cert), -60) &&
             X509_gmtime_adj(X509_getm_notAfter(cert), 3600) && X509_set_pubkey(cert, key) &&
             X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"localhost",
                                        -1, -1, 0) &&
             X509_set_subject_name(cert, name) && X509_set_issuer_name(cert, name) &&
             X509_sign(cert, key, EVP_sha256());
    X509_NAME_free(name);
    if (!ok) {
        X509_free(cert);
        return NULL;
    }
    return cert;
}

/* One step of `ssl`'s handshake: whether it is done or waits for its peer. */
static int step(SSL *ssl) {
    int result = SSL_do_handshake(ssl);
    return result == 1 || SSL_get_error(ssl, result) == SSL_ERROR_WANT_READ;
}

static int tls13_carries_a_message(void) {
    EVP_PKEY *key = EVP_EC_gen("P-256");
    X509 *cert = key ? self_signed(key) : NULL;
    SSL_CTX *server_ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX *client_ctx = SSL_CTX_new(TLS_client_method());
    SSL *server = NULL, *client = NULL;
    BIO *server_end = NULL, *client_end = NULL;
    char received[5] = {0};
    int ok = 0, i;

    if (!cert || !server_ctx || !client_ctx) goto done;
    if (SSL_CTX_use_certificate(server_ctx, cert) != 1 || SSL_CTX_use_PrivateKey(server_ctx, key) != 1 ||
        !SSL_CTX_set_min_proto_version(server_ctx, TLS1_3_VERSION) ||
        !SSL_CTX_set_min_proto_version(client_ctx, TLS1_3_VERSION) ||
        X509_STORE_add_cert(SSL_CTX_get_cert_store(client_ctx), cert) != 1) {
        goto done;
    }
    SSL_CTX_set_verify(client_ctx, SSL_VERIFY_PEER, NULL);
    server = SSL_new(server_ctx);
    client = SSL_new(client_ctx);
    if (!server || !client || BIO_new_bio_pair(&server_end, 0, &client_end, 0) != 1) goto done;
    /* Each takes its end of the pipe. */
    SSL_set_bio(server, server_end, server_end);
    SSL_set_bio(client, client_end, client_end);
    SSL_set_accept_state(server);
    SSL_set_connect_state(client);
    for (i = 0; i < 16 && !(SSL_is_init_finished(client) && SSL_is_init_finished(server)); i++) {
        if (!step(client) || !step(server)) goto done;
    }
    ok = SSL_is_init_finished(client) && SSL_is_init_finished(server) &&
         SSL_version(client) == TLS1_3_VERSION && SSL_get_verify_result(client) == X509_V_OK &&
         SSL_write(client, "ping", 4) == 4 && SSL_read(server, received, 4) == 4 &&
         memcmp(received, "ping", 4) == 0;
done:
    SSL_free(client);
    SSL_free(server);
    SSL_CTX_free(client_ctx);
    SSL_CTX_free(server_ctx);
    X509_free(cert);
    EVP_PKEY_free(key);
    return ok;
}

int main(void) {
    if (!runs_what_the_headers_describe()) {
        fprintf(stderr, "OpenSSL %s runs under the headers of %s\n",
                OpenSSL_version(OPENSSL_VERSION_STRING), OPENSSL_VERSION_STR);
        return 3;
    }
    if (!sha256_answers()) {
        fputs("SHA-256 of \"abc\" is wrong\n", stderr);
        return 1;
    }
    if (!tls13_carries_a_message()) {
        fputs("no TLS 1.3 message between a client and a server\n", stderr);
        return 2;
    }
    return 0;
}
