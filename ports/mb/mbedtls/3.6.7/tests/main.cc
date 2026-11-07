// The C++ consumer: a client and a server connected in memory complete a
// TLS 1.3 handshake and then a TLS 1.2 one with upstream's self-signed test
// certificate and key (framework/data_files/server5-selfsigned.crt and
// server5.key), the client verifying the server against that certificate,
// and exchange application data both ways, a record altered in transit
// being rejected; a client expecting another host name refuses the server.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

static bool ok = true;

static void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

static const char certificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBxzCCAW2gAwIBAgIMU6LLSxJOrYN9qJSyMAoGCCqGSM49BAMCMEcxEzARBgNV\n"
    "BAMMCnNlbGZzaWduZWQxEDAOBgNVBAsMB3Rlc3RpbmcxETAPBgNVBAoMCFBvbGFy\n"
    "U1NMMQswCQYDVQQGEwJOTDAeFw0yMzA1MDkwMjQ5NTdaFw0zMzA1MDYwMjQ5NTda\n"
    "MEcxEzARBgNVBAMMCnNlbGZzaWduZWQxEDAOBgNVBAsMB3Rlc3RpbmcxETAPBgNV\n"
    "BAoMCFBvbGFyU1NMMQswCQYDVQQGEwJOTDBZMBMGByqGSM49AgEGCCqGSM49AwEH\n"
    "A0IABDfMVtl2CR5acj7HWS3/IG7ufPkGkXTQrRS192giWWKSTuUA2CMR/+ov0jRd\n"
    "XRa9iojCa3cNVc2KKg76Aci07f+jPzA9MAwGA1UdEwEB/wQCMAAwDgYDVR0PAQH/\n"
    "BAQDAgeAMB0GA1UdDgQWBBRQYaWP1AfZ14IBDOVlf4xjRqcTvjAKBggqhkjOPQQD\n"
    "AgNIADBFAiAXiJxDrd5aLzGB/Uc3kYBIBuSUIMGvol2c8EvwmF3zmQIhAPFrKMgA\n"
    "s2awzo/PBB5gFTkDub88wRYwS1R9JPYCXUO0\n"
    "-----END CERTIFICATE-----\n";

static const char private_key[] =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIPEqEyB2AnCoPL/9U/YDHvdqXYbIogTywwyp6/UfDw6noAoGCCqGSM49\n"
    "AwEHoUQDQgAEN8xW2XYJHlpyPsdZLf8gbu58+QaRdNCtFLX3aCJZYpJO5QDYIxH/\n"
    "6i/SNF1dFr2KiMJrdw1VzYoqDvoByLTt/w==\n"
    "-----END EC PRIVATE KEY-----\n";

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt cert;
static mbedtls_pk_context key;

// One direction of the in-memory connection.
using Pipe = std::deque<unsigned char>;

struct Endpoint {
    Pipe *in;
    Pipe *out;
};

static int send_bytes(void *ctx, const unsigned char *buf, size_t len) {
    Endpoint *end = static_cast<Endpoint *>(ctx);
    end->out->insert(end->out->end(), buf, buf + len);
    return static_cast<int>(len);
}

static int receive_bytes(void *ctx, unsigned char *buf, size_t len) {
    Endpoint *end = static_cast<Endpoint *>(ctx);
    if (end->in->empty())
        return MBEDTLS_ERR_SSL_WANT_READ;
    const size_t n = std::min(len, end->in->size());
    std::copy(end->in->begin(), end->in->begin() + static_cast<std::ptrdiff_t>(n), buf);
    end->in->erase(end->in->begin(), end->in->begin() + static_cast<std::ptrdiff_t>(n));
    return static_cast<int>(n);
}

// The certificate expires in 2033; only expiry is excused.
static int excuse_expiry(void *, mbedtls_x509_crt *, int, uint32_t *flags) {
    *flags &= ~static_cast<uint32_t>(MBEDTLS_X509_BADCERT_EXPIRED);
    return 0;
}

struct Tls {
    mbedtls_ssl_config conf;
    mbedtls_ssl_context ssl;
    Tls() {
        mbedtls_ssl_config_init(&conf);
        mbedtls_ssl_init(&ssl);
    }
    ~Tls() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
    }
};

static bool pending(int ret) {
    return ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE;
}

// Reads one application message, past any post-handshake messages.
static std::string read_message(mbedtls_ssl_context *ssl) {
    unsigned char buf[256];
    for (int i = 0; i < 10; ++i) {
        const int ret = mbedtls_ssl_read(ssl, buf, sizeof buf);
        if (ret > 0)
            return std::string(reinterpret_cast<const char *>(buf), static_cast<size_t>(ret));
        if (!pending(ret) && ret != MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            break;
    }
    return std::string();
}

static bool write_message(mbedtls_ssl_context *ssl, const std::string &message) {
    return mbedtls_ssl_write(ssl, reinterpret_cast<const unsigned char *>(message.data()), message.size()) ==
           static_cast<int>(message.size());
}

struct Session {
    int client;
    int server;
    uint32_t verify;
    std::string version;
    std::string ciphersuite;
    std::string peer;
    std::string to_server;
    std::string to_client;
    int tampered;
};

static Session connect(mbedtls_ssl_protocol_version version, const char *hostname) {
    Pipe to_server, to_client;
    Endpoint client_end = {&to_client, &to_server};
    Endpoint server_end = {&to_server, &to_client};
    Tls client, server;
    Session session = {MBEDTLS_ERR_SSL_WANT_READ, MBEDTLS_ERR_SSL_WANT_READ, 0, "", "", "", "", "", 0};

    check(mbedtls_ssl_config_defaults(&server.conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT) == 0,
          "server configuration");
    mbedtls_ssl_conf_rng(&server.conf, mbedtls_ctr_drbg_random, &drbg);
    check(mbedtls_ssl_conf_own_cert(&server.conf, &cert, &key) == 0, "the server takes the certificate and key");
    mbedtls_ssl_conf_max_tls_version(&server.conf, version);

    check(mbedtls_ssl_config_defaults(&client.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT) == 0,
          "client configuration");
    mbedtls_ssl_conf_rng(&client.conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_conf_authmode(&client.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&client.conf, &cert, nullptr);
    mbedtls_ssl_conf_verify(&client.conf, excuse_expiry, nullptr);
    mbedtls_ssl_conf_max_tls_version(&client.conf, version);

    check(mbedtls_ssl_setup(&server.ssl, &server.conf) == 0 && mbedtls_ssl_setup(&client.ssl, &client.conf) == 0,
          "both ends set up");
    check(mbedtls_ssl_set_hostname(&client.ssl, hostname) == 0, "the client takes the host name");
    mbedtls_ssl_set_bio(&client.ssl, &client_end, send_bytes, receive_bytes, nullptr);
    mbedtls_ssl_set_bio(&server.ssl, &server_end, send_bytes, receive_bytes, nullptr);

    for (int i = 0; i < 100 && (pending(session.client) || pending(session.server)); ++i) {
        if (pending(session.client))
            session.client = mbedtls_ssl_handshake(&client.ssl);
        if (pending(session.server))
            session.server = mbedtls_ssl_handshake(&server.ssl);
    }
    session.verify = mbedtls_ssl_get_verify_result(&client.ssl);
    if (session.client != 0 || session.server != 0)
        return session;

    session.version = mbedtls_ssl_get_version(&client.ssl);
    session.ciphersuite = mbedtls_ssl_get_ciphersuite(&client.ssl);
    if (const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&client.ssl)) {
        char subject[128];
        if (mbedtls_x509_dn_gets(subject, sizeof subject, &peer->subject) > 0)
            session.peer = subject;
    }
    if (write_message(&client.ssl, "ping from the client"))
        session.to_server = read_message(&server.ssl);
    if (write_message(&server.ssl, "pong from the server"))
        session.to_client = read_message(&client.ssl);
    // One bit flipped in the record's last byte, part of its authentication tag.
    if (write_message(&client.ssl, "altered in transit") && !to_server.empty()) {
        to_server.back() ^= 1;
        unsigned char buf[64];
        session.tampered = mbedtls_ssl_read(&server.ssl, buf, sizeof buf);
    }
    return session;
}

static void handshakes(mbedtls_ssl_protocol_version version, const char *name, const char *suite_prefix) {
    const Session session = connect(version, "selfsigned");
    const std::string what = std::string(name) + ": ";
    check(session.client == 0 && session.server == 0, (what + "both ends complete the handshake").c_str());
    check(session.verify == 0, (what + "the client verifies the server").c_str());
    check(session.version == name, (what + "the negotiated version").c_str());
    check(session.ciphersuite.compare(0, std::strlen(suite_prefix), suite_prefix) == 0,
          (what + "an ECDHE-ECDSA or TLS 1.3 suite").c_str());
    check(session.peer == "CN=selfsigned, OU=testing, O=PolarSSL, C=NL", (what + "the peer certificate").c_str());
    check(session.to_server == "ping from the client", (what + "the server reads the client's message").c_str());
    check(session.to_client == "pong from the server", (what + "the client reads the server's reply").c_str());
    check(session.tampered == MBEDTLS_ERR_SSL_INVALID_MAC, (what + "an altered record is rejected").c_str());
}

static void refuses_other_host() {
    const Session session = connect(MBEDTLS_SSL_VERSION_TLS1_3, "other.example");
    check(session.client == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, "a client expecting another host refuses");
    check((session.verify & MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0, "the refusal is a name mismatch");
    check(session.server != 0, "the server's handshake does not complete");
}

int main() {
    check(psa_crypto_init() == PSA_SUCCESS, "PSA crypto initializes");
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&cert);
    mbedtls_pk_init(&key);
    static const unsigned char personalization[] = "cabin-ports mbedtls";
    check(mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, personalization,
                                sizeof personalization - 1) == 0,
          "the DRBG seeds from the platform's entropy");
    check(mbedtls_x509_crt_parse(&cert, reinterpret_cast<const unsigned char *>(certificate), sizeof certificate) ==
              0,
          "the certificate parses");
    check(mbedtls_pk_parse_key(&key, reinterpret_cast<const unsigned char *>(private_key), sizeof private_key,
                               nullptr, 0, mbedtls_ctr_drbg_random, &drbg) == 0,
          "the key parses");
    check(mbedtls_pk_check_pair(&cert.pk, &key, mbedtls_ctr_drbg_random, &drbg) == 0,
          "the key belongs to the certificate");

    if (ok) {
        handshakes(MBEDTLS_SSL_VERSION_TLS1_3, "TLSv1.3", "TLS1-3-");
        handshakes(MBEDTLS_SSL_VERSION_TLS1_2, "TLSv1.2", "TLS-ECDHE-ECDSA-");
        refuses_other_host();
    }

    mbedtls_pk_free(&key);
    mbedtls_x509_crt_free(&cert);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_psa_crypto_free();
    if (!ok) {
        std::fprintf(stderr, "mbedtls (C++): FAILED\n");
        return 1;
    }
    std::printf("mbedtls (C++): ok\n");
    return 0;
}
