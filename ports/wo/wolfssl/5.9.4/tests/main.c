/* Drives wolfSSL from C: wolfCrypt known-answer tests (SHA-256, HMAC,
   AES-GCM), ECDSA over P-256 with the hash DRBG, and TLS 1.3 and 1.2
   handshakes between a client and a server in one process, over memory
   buffers, with the ECC test chain in <wolfssl/certs_test.h>; a client
   trusting another CA must fail the handshake. */
#define USE_CERT_BUFFERS_256
#define USE_CERT_BUFFERS_2048

#include <wolfssl/options.h>
/* Like configure's, options.h alone carries the build's configuration. */
#if !defined(HAVE_ECC) || !defined(WOLFSSL_TLS13) || !defined(NO_MD5)
#error "wolfssl/options.h does not carry the build configuration"
#endif
#include <wolfssl/ssl.h>
#include <wolfssl/certs_test.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/sha256.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, int line, const char *what)
{
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

static int hex_eq(const unsigned char *bytes, const char *hex)
{
  size_t i, n = strlen(hex) / 2;
  for (i = 0; i < n; ++i) {
    unsigned int v;
    if (sscanf(hex + 2 * i, "%2x", &v) != 1 || bytes[i] != v) return 0;
  }
  return 1;
}

static void hashes(void)
{
  static const char jefe[] = "Jefe";
  static const char want[] = "what do ya want for nothing?";
  unsigned char digest[WC_SHA256_DIGEST_SIZE];
  wc_Sha256 sha;
  Hmac hmac;

  CHECK(wc_InitSha256(&sha) == 0);
  CHECK(wc_Sha256Update(&sha, (const byte *)"abc", 3) == 0);
  CHECK(wc_Sha256Final(&sha, digest) == 0);
  wc_Sha256Free(&sha);
  CHECK(hex_eq(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

  /* RFC 4231, test case 2. */
  CHECK(wc_HmacInit(&hmac, NULL, INVALID_DEVID) == 0);
  CHECK(wc_HmacSetKey(&hmac, WC_SHA256, (const byte *)jefe, 4) == 0);
  CHECK(wc_HmacUpdate(&hmac, (const byte *)want, (word32)strlen(want)) == 0);
  CHECK(wc_HmacFinal(&hmac, digest) == 0);
  wc_HmacFree(&hmac);
  CHECK(hex_eq(digest, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
}

static void aes_gcm(void)
{
  /* The GCM specification's test case 2: zero key, IV and block. */
  byte key[16] = {0}, iv[12] = {0}, plain[16] = {0}, cipher[16], tag[16], back[16];
  Aes aes;

  CHECK(wc_AesInit(&aes, NULL, INVALID_DEVID) == 0);
  CHECK(wc_AesGcmSetKey(&aes, key, sizeof key) == 0);
  CHECK(wc_AesGcmEncrypt(&aes, cipher, plain, sizeof plain, iv, sizeof iv, tag, sizeof tag, NULL, 0) == 0);
  CHECK(hex_eq(cipher, "0388dace60b6a392f328c2b971b2fe78"));
  CHECK(hex_eq(tag, "ab6e47d42cec13bdf53a67b21257bddf"));
  CHECK(wc_AesGcmDecrypt(&aes, back, cipher, sizeof cipher, iv, sizeof iv, tag, sizeof tag, NULL, 0) == 0);
  CHECK(memcmp(back, plain, sizeof plain) == 0);
  tag[0] ^= 1;
  CHECK(wc_AesGcmDecrypt(&aes, back, cipher, sizeof cipher, iv, sizeof iv, tag, sizeof tag, NULL, 0) ==
        AES_GCM_AUTH_E);
  wc_AesFree(&aes);
}

static void ecdsa(void)
{
  byte hash[WC_SHA256_DIGEST_SIZE];
  byte sig[ECC_MAX_SIG_SIZE];
  word32 sig_len = sizeof sig;
  int verified = 0;
  WC_RNG rng;
  ecc_key key;

  memset(hash, 0x5a, sizeof hash);
  CHECK(wc_InitRng(&rng) == 0);
  CHECK(wc_ecc_init(&key) == 0);
  CHECK(wc_ecc_make_key_ex(&rng, 32, &key, ECC_SECP256R1) == 0);
  CHECK(wc_ecc_check_key(&key) == 0);
  CHECK(wc_ecc_sign_hash(hash, sizeof hash, sig, &sig_len, &rng, &key) == 0);
  CHECK(wc_ecc_verify_hash(sig, sig_len, hash, sizeof hash, &verified, &key) == 0 && verified == 1);
  hash[0] ^= 1;
  CHECK(wc_ecc_verify_hash(sig, sig_len, hash, sizeof hash, &verified, &key) == 0 && verified == 0);
  wc_ecc_free(&key);
  wc_FreeRng(&rng);
}

/* One direction of the connection. */
typedef struct {
  unsigned char data[32768];
  int len;
} Pipe;

static int pipe_recv(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
  Pipe *p = (Pipe *)ctx;
  (void)ssl;
  if (p->len == 0) return WOLFSSL_CBIO_ERR_WANT_READ;
  if (sz > p->len) sz = p->len;
  memcpy(buf, p->data, (size_t)sz);
  memmove(p->data, p->data + sz, (size_t)(p->len - sz));
  p->len -= sz;
  return sz;
}

static int pipe_send(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
  Pipe *p = (Pipe *)ctx;
  (void)ssl;
  if (sz > (int)sizeof p->data - p->len) return WOLFSSL_CBIO_ERR_WANT_WRITE;
  memcpy(p->data + p->len, buf, (size_t)sz);
  p->len += sz;
  return sz;
}

/* The test certificates expire in 2029; accept a date error, and only that,
   so the chain and signatures stay checked. */
static int accept_dates(int preverify, WOLFSSL_X509_STORE_CTX *store)
{
  return preverify || store->error == ASN_AFTER_DATE_E || store->error == ASN_BEFORE_DATE_E;
}

static Pipe to_server, to_client;

static WOLFSSL *attach(WOLFSSL_CTX *ctx, Pipe *in, Pipe *out)
{
  WOLFSSL *ssl = wolfSSL_new(ctx);
  CHECK(ssl != NULL);
  if (!ssl) return NULL;
  wolfSSL_SSLSetIORecv(ssl, pipe_recv);
  wolfSSL_SSLSetIOSend(ssl, pipe_send);
  wolfSSL_SetIOReadCtx(ssl, in);
  wolfSSL_SetIOWriteCtx(ssl, out);
  return ssl;
}

static int pending(WOLFSSL *ssl, int ret)
{
  int err = wolfSSL_get_error(ssl, ret);
  return err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE;
}

/* Runs both ends until each finishes or one fails; returns 1 when both
   finished, else stores the client's error. */
static int handshake(WOLFSSL *client, WOLFSSL *server, int *client_err)
{
  int client_done = 0, server_done = 0, round;
  *client_err = 0;
  for (round = 0; round < 64 && !(client_done && server_done); ++round) {
    if (!client_done) {
      int ret = wolfSSL_connect(client);
      if (ret == WOLFSSL_SUCCESS) client_done = 1;
      else if (!pending(client, ret)) {
        *client_err = wolfSSL_get_error(client, ret);
        return 0;
      }
    }
    if (!server_done) {
      int ret = wolfSSL_accept(server);
      if (ret == WOLFSSL_SUCCESS) server_done = 1;
      else if (!pending(server, ret)) return 0;
    }
  }
  return client_done && server_done;
}

static WOLFSSL_CTX *server_ctx(void)
{
  WOLFSSL_CTX *ctx = wolfSSL_CTX_new(wolfSSLv23_server_method());
  CHECK(ctx != NULL);
  if (!ctx) return NULL;
  CHECK(wolfSSL_CTX_use_certificate_buffer(ctx, serv_ecc_der_256, sizeof_serv_ecc_der_256,
                                           WOLFSSL_FILETYPE_ASN1) == WOLFSSL_SUCCESS);
  CHECK(wolfSSL_CTX_use_PrivateKey_buffer(ctx, ecc_key_der_256, sizeof_ecc_key_der_256,
                                          WOLFSSL_FILETYPE_ASN1) == WOLFSSL_SUCCESS);
  return ctx;
}

static WOLFSSL_CTX *client_ctx(WOLFSSL_METHOD *method, const unsigned char *ca, long ca_len)
{
  WOLFSSL_CTX *ctx = wolfSSL_CTX_new(method);
  CHECK(ctx != NULL);
  if (!ctx) return NULL;
  CHECK(wolfSSL_CTX_load_verify_buffer_ex(ctx, ca, ca_len, WOLFSSL_FILETYPE_ASN1, 0,
                                          WOLFSSL_LOAD_FLAG_DATE_ERR_OKAY) == WOLFSSL_SUCCESS);
  wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER, accept_dates);
  return ctx;
}

static void session(WOLFSSL_METHOD *method, const char *version, const char *cipher_prefix)
{
  WOLFSSL_CTX *sctx = server_ctx();
  WOLFSSL_CTX *cctx = client_ctx(method, ca_ecc_cert_der_256, sizeof_ca_ecc_cert_der_256);
  WOLFSSL *server, *client;
  char reply[16] = {0};
  int err;

  if (!sctx || !cctx) return;
  to_server.len = to_client.len = 0;
  client = attach(cctx, &to_client, &to_server);
  server = attach(sctx, &to_server, &to_client);
  if (client && server) {
    CHECK(handshake(client, server, &err));
    CHECK(strcmp(wolfSSL_get_version(client), version) == 0);
    CHECK(strcmp(wolfSSL_get_version(server), version) == 0);
    CHECK(strncmp(wolfSSL_get_cipher_name(client), cipher_prefix, strlen(cipher_prefix)) == 0);
    CHECK(wolfSSL_write(client, "ping", 4) == 4);
    CHECK(wolfSSL_read(server, reply, sizeof reply) == 4 && memcmp(reply, "ping", 4) == 0);
    CHECK(wolfSSL_write(server, "pong", 4) == 4);
    CHECK(wolfSSL_read(client, reply, sizeof reply) == 4 && memcmp(reply, "pong", 4) == 0);
  }
  wolfSSL_free(client);
  wolfSSL_free(server);
  wolfSSL_CTX_free(cctx);
  wolfSSL_CTX_free(sctx);
}

static void untrusted(void)
{
  /* The RSA test CA did not sign the server's ECC certificate. */
  WOLFSSL_CTX *sctx = server_ctx();
  WOLFSSL_CTX *cctx = client_ctx(wolfTLSv1_3_client_method(), ca_cert_der_2048, sizeof_ca_cert_der_2048);
  WOLFSSL *server, *client;
  int err;

  if (!sctx || !cctx) return;
  to_server.len = to_client.len = 0;
  client = attach(cctx, &to_client, &to_server);
  server = attach(sctx, &to_server, &to_client);
  if (client && server) {
    CHECK(!handshake(client, server, &err));
    CHECK(err == ASN_NO_SIGNER_E);
  }
  wolfSSL_free(client);
  wolfSSL_free(server);
  wolfSSL_CTX_free(cctx);
  wolfSSL_CTX_free(sctx);
}

int main(void)
{
  CHECK(wolfSSL_Init() == WOLFSSL_SUCCESS);
  CHECK(strcmp(wolfSSL_lib_version(), "5.9.4") == 0);
  hashes();
  aes_gcm();
  ecdsa();
  session(wolfTLSv1_3_client_method(), "TLSv1.3", "TLS13-");
  session(wolfTLSv1_2_client_method(), "TLSv1.2", "ECDHE-ECDSA-");
  untrusted();
  CHECK(wolfSSL_Cleanup() == WOLFSSL_SUCCESS);

  printf("wolfSSL C: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
