/* Drives libsrtp from C under the default policy, AES_CM_128_HMAC_SHA1_80:
   srtp_init, whose crypto kernel runs the AES-ICM and HMAC-SHA1 self-tests;
   the SRTP known answer of upstream's srtp_validate() (test/srtp_driver.c),
   which RFC 3711's key derivation over OpenSSL's AES reproduces; receiving
   it; replayed and tampered packets; the version; and the AES-GCM policy
   that this build, without OpenSSL, refuses. */
#include <srtp2/auth.h>
#include <srtp2/cipher.h>
#include <srtp2/crypto_types.h>
#include <srtp2/srtp.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/* srtp_driver.c's test_key: a 16-byte master key, then a 14-byte salt. */
static unsigned char key[SRTP_MASTER_KEY_LEN] = {
    0xe1, 0xf9, 0x7a, 0x0d, 0x3e, 0x01, 0x8b, 0xe0, 0xd6, 0x4f,
    0xa3, 0x2c, 0x06, 0xde, 0x41, 0x39, 0x0e, 0xc6, 0x75, 0xad,
    0x49, 0x8a, 0xfe, 0xeb, 0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6
};

/* Sequence number 0x1234, SSRC 0xcafebabe, 16 bytes of payload. */
static const unsigned char plaintext[28] = {
    0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe,
    0xba, 0xbe, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
    0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab
};

static const unsigned char ciphertext[38] = {
    0x80, 0x0f, 0x12, 0x34, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe,
    0xba, 0xbe, 0x4e, 0x55, 0xdc, 0x4c, 0xe7, 0x99, 0x78, 0xd8,
    0x8c, 0xa4, 0xd2, 0x15, 0x94, 0x9d, 0x24, 0x02, 0xb7, 0x8d,
    0x6a, 0xcc, 0x99, 0xea, 0x17, 0x9b, 0x8d, 0xbb
};

/* srtp_protect and srtp_unprotect take a packet aligned on 32 bits with
   room for the trailer after it (srtp.h). */
typedef union {
    uint32_t align;
    unsigned char b[sizeof plaintext + SRTP_MAX_TRAILER_LEN];
} packet_buffer;

int main(void)
{
    srtp_policy_t policy;
    srtp_t sender = NULL;
    srtp_t receiver = NULL;
    srtp_t gcm_session = NULL;
    packet_buffer packet;
    packet_buffer next;
    packet_buffer tampered;
    uint32_t trailer = 0;
    int len;

    check(srtp_init() == srtp_err_status_ok, "srtp_init");
    check(srtp_get_version() == 0x02080002u, "srtp_get_version");
    check(strcmp(srtp_get_version_string(), "libsrtp2 2.8.2") == 0,
          "srtp_get_version_string");

    memset(&policy, 0, sizeof policy);
    srtp_crypto_policy_set_rtp_default(&policy.rtp);
    srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
    check(policy.rtp.cipher_type == SRTP_AES_ICM_128 &&
              policy.rtp.cipher_key_len == SRTP_AES_ICM_128_KEY_LEN_WSALT &&
              policy.rtp.auth_type == SRTP_HMAC_SHA1 &&
              policy.rtp.auth_tag_len == 10 &&
              policy.rtp.sec_serv == sec_serv_conf_and_auth,
          "default RTP policy");
    policy.ssrc.type = ssrc_specific;
    policy.ssrc.value = 0xcafebabe;
    policy.key = key;
    policy.window_size = 128;
    check(srtp_create(&sender, &policy) == srtp_err_status_ok, "sender");
    check(srtp_create(&receiver, &policy) == srtp_err_status_ok, "receiver");
    if (failures) {
        return 1;
    }
    check(srtp_get_protect_trailer_length(sender, 0, 0, &trailer) ==
                  srtp_err_status_ok &&
              trailer == 10,
          "an 80-bit tag");

    memcpy(packet.b, plaintext, sizeof plaintext);
    len = (int)sizeof plaintext;
    check(srtp_protect(sender, packet.b, &len) == srtp_err_status_ok,
          "srtp_protect");
    check(len == (int)sizeof ciphertext, "protected length");
    check(memcmp(packet.b, ciphertext, sizeof ciphertext) == 0,
          "SRTP known answer");

    len = (int)sizeof ciphertext;
    check(srtp_unprotect(receiver, packet.b, &len) == srtp_err_status_ok,
          "srtp_unprotect");
    check(len == (int)sizeof plaintext &&
              memcmp(packet.b, plaintext, sizeof plaintext) == 0,
          "unprotected payload");

    memcpy(packet.b, ciphertext, sizeof ciphertext);
    len = (int)sizeof ciphertext;
    check(srtp_unprotect(receiver, packet.b, &len) ==
              srtp_err_status_replay_fail,
          "replayed packet");

    /* The next sequence number, so the replay check passes and only the
       tag can reject the flipped payload bit. */
    memcpy(next.b, plaintext, sizeof plaintext);
    next.b[3] = 0x35;
    len = (int)sizeof plaintext;
    check(srtp_protect(sender, next.b, &len) == srtp_err_status_ok &&
              len == (int)sizeof ciphertext,
          "second packet");
    memcpy(tampered.b, next.b, sizeof ciphertext);
    tampered.b[20] ^= 0x01;
    len = (int)sizeof ciphertext;
    check(srtp_unprotect(receiver, tampered.b, &len) ==
              srtp_err_status_auth_fail,
          "tampered payload");
    memcpy(tampered.b, next.b, sizeof ciphertext);
    tampered.b[sizeof ciphertext - 1] ^= 0x80;
    len = (int)sizeof ciphertext;
    check(srtp_unprotect(receiver, tampered.b, &len) ==
              srtp_err_status_auth_fail,
          "tampered tag");
    len = (int)sizeof ciphertext;
    check(srtp_unprotect(receiver, next.b, &len) == srtp_err_status_ok &&
              len == (int)sizeof plaintext && next.b[3] == 0x35 &&
              memcmp(next.b + 12, plaintext + 12, 16) == 0,
          "second packet after the rejected copies");

    srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy.rtp);
    srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy.rtcp);
    check(srtp_create(&gcm_session, &policy) == srtp_err_status_fail,
          "no AES-GCM cipher without OpenSSL");

    check(srtp_dealloc(sender) == srtp_err_status_ok, "srtp_dealloc sender");
    check(srtp_dealloc(receiver) == srtp_err_status_ok,
          "srtp_dealloc receiver");
    check(srtp_shutdown() == srtp_err_status_ok, "srtp_shutdown");

    if (failures) {
        return 1;
    }
    puts("libsrtp C consumer: ok");
    return 0;
}
