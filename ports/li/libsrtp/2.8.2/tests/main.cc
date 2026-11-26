// Drives libsrtp from C++: the SRTCP known answer of upstream's
// srtp_validate() (test/srtp_driver.c) under the default RTCP policy, which
// RFC 3711's key derivation over OpenSSL's AES reproduces; receiving it and
// rejecting a tampered tag; and a session built from the DTLS-SRTP profile
// SRTP_AES128_CM_SHA1_32, whose RTP tag is 32 bits and RTCP tag 80.
#include <srtp2/auth.h>
#include <srtp2/cipher.h>
#include <srtp2/crypto_types.h>
#include <srtp2/srtp.h>

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// srtp_driver.c's test_key: a 16-byte master key, then a 14-byte salt.
unsigned char key[SRTP_MASTER_KEY_LEN] = {
    0xe1, 0xf9, 0x7a, 0x0d, 0x3e, 0x01, 0x8b, 0xe0, 0xd6, 0x4f,
    0xa3, 0x2c, 0x06, 0xde, 0x41, 0x39, 0x0e, 0xc6, 0x75, 0xad,
    0x49, 0x8a, 0xfe, 0xeb, 0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6};

// A sender report header from SSRC 0xcafebabe, then 16 bytes.
const unsigned char rtcp[24] = {
    0x81, 0xc8, 0x00, 0x0b, 0xca, 0xfe, 0xba, 0xbe, 0xab, 0xab, 0xab, 0xab,
    0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab};

// Encrypted, then SRTCP index 1 with the E bit, then the 80-bit tag.
const unsigned char srtcp[38] = {
    0x81, 0xc8, 0x00, 0x0b, 0xca, 0xfe, 0xba, 0xbe, 0x71, 0x28,
    0x03, 0x5b, 0xe4, 0x87, 0xb9, 0xbd, 0xbe, 0xf8, 0x90, 0x41,
    0xf9, 0x77, 0xa5, 0xa8, 0x80, 0x00, 0x00, 0x01, 0x99, 0x3e,
    0x08, 0xcd, 0x54, 0xd6, 0xc1, 0x23, 0x07, 0x98};

// srtp_protect and srtp_protect_rtcp take a packet aligned on 32 bits with
// room for the trailer after it (srtp.h); 32 bytes is the longest packet here.
union packet_buffer {
  uint32_t align;
  unsigned char b[32 + SRTP_MAX_SRTCP_TRAILER_LEN];
};

srtp_policy_t make_policy() {
  srtp_policy_t policy;
  std::memset(&policy, 0, sizeof policy);
  srtp_crypto_policy_set_rtp_default(&policy.rtp);
  srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
  policy.ssrc.type = ssrc_specific;
  policy.ssrc.value = 0xcafebabe;
  policy.key = key;
  policy.window_size = 128;
  return policy;
}

void srtcp_known_answer() {
  srtp_policy_t policy = make_policy();
  srtp_t sender = 0;
  srtp_t receiver = 0;
  check(srtp_create(&sender, &policy) == srtp_err_status_ok, "sender");
  check(srtp_create(&receiver, &policy) == srtp_err_status_ok, "receiver");
  if (!sender || !receiver) return;

  packet_buffer packet;
  std::memcpy(packet.b, rtcp, sizeof rtcp);
  int len = static_cast<int>(sizeof rtcp);
  check(srtp_protect_rtcp(sender, packet.b, &len) == srtp_err_status_ok,
        "srtp_protect_rtcp");
  check(len == static_cast<int>(sizeof srtcp), "protected SRTCP length");
  check(std::memcmp(packet.b, srtcp, sizeof srtcp) == 0,
        "SRTCP known answer");

  packet_buffer tampered;
  std::memcpy(tampered.b, srtcp, sizeof srtcp);
  tampered.b[sizeof srtcp - 1] ^= 0x01;
  len = static_cast<int>(sizeof srtcp);
  check(srtp_unprotect_rtcp(receiver, tampered.b, &len) ==
            srtp_err_status_auth_fail,
        "tampered SRTCP tag");

  len = static_cast<int>(sizeof srtcp);
  check(srtp_unprotect_rtcp(receiver, packet.b, &len) == srtp_err_status_ok,
        "srtp_unprotect_rtcp");
  check(len == static_cast<int>(sizeof rtcp) &&
            std::memcmp(packet.b, rtcp, sizeof rtcp) == 0,
        "unprotected RTCP");

  check(srtp_dealloc(sender) == srtp_err_status_ok, "srtp_dealloc sender");
  check(srtp_dealloc(receiver) == srtp_err_status_ok,
        "srtp_dealloc receiver");
}

void sha1_32_profile() {
  const srtp_profile_t profile = srtp_profile_aes128_cm_sha1_32;
  check(srtp_profile_get_master_key_length(profile) == 16 &&
            srtp_profile_get_master_salt_length(profile) == 14,
        "SRTP_AES128_CM_SHA1_32 key and salt lengths");

  srtp_policy_t policy = make_policy();
  check(srtp_crypto_policy_set_from_profile_for_rtp(&policy.rtp, profile) ==
                srtp_err_status_ok &&
            srtp_crypto_policy_set_from_profile_for_rtcp(&policy.rtcp,
                                                         profile) ==
                srtp_err_status_ok,
        "policy from the profile");
  check(policy.rtp.cipher_type == SRTP_AES_ICM_128 &&
            policy.rtp.auth_type == SRTP_HMAC_SHA1 &&
            policy.rtp.auth_tag_len == 4 && policy.rtcp.auth_tag_len == 10,
        "SRTP_AES128_CM_SHA1_32 tags");
  policy.ssrc.type = ssrc_any_outbound;
  srtp_t sender = 0;
  check(srtp_create(&sender, &policy) == srtp_err_status_ok,
        "profile sender");
  policy.ssrc.type = ssrc_any_inbound;
  srtp_t receiver = 0;
  check(srtp_create(&receiver, &policy) == srtp_err_status_ok,
        "profile receiver");
  if (!sender || !receiver) return;

  uint32_t trailer = 0;
  check(srtp_get_protect_trailer_length(sender, 0, 0, &trailer) ==
                srtp_err_status_ok &&
            trailer == 4,
        "a 32-bit SRTP tag");
  check(srtp_get_protect_rtcp_trailer_length(sender, 0, 0, &trailer) ==
                srtp_err_status_ok &&
            trailer == 14,
        "SRTCP index and 80-bit tag");

  // Sequence number 7, SSRC 0x01020304, a 20-byte payload.
  unsigned char rtp[32] = {0x80, 0x60, 0x00, 0x07, 0x00, 0x00, 0x03, 0xe8,
                           0x01, 0x02, 0x03, 0x04};
  for (int i = 12; i < 32; ++i) rtp[i] = static_cast<unsigned char>(i * 7);
  packet_buffer packet;
  std::memcpy(packet.b, rtp, sizeof rtp);
  int len = static_cast<int>(sizeof rtp);
  check(srtp_protect(sender, packet.b, &len) == srtp_err_status_ok,
        "srtp_protect under the profile");
  check(len == static_cast<int>(sizeof rtp) + 4, "4-byte tag appended");
  check(std::memcmp(packet.b, rtp, 12) == 0 &&
            std::memcmp(packet.b + 12, rtp + 12, 20) != 0,
        "header in clear, payload encrypted");
  check(srtp_unprotect(receiver, packet.b, &len) == srtp_err_status_ok &&
            len == static_cast<int>(sizeof rtp) &&
            std::memcmp(packet.b, rtp, sizeof rtp) == 0,
        "round trip under the profile");

  check(srtp_dealloc(sender) == srtp_err_status_ok, "srtp_dealloc sender");
  check(srtp_dealloc(receiver) == srtp_err_status_ok,
        "srtp_dealloc receiver");
}

}  // namespace

int main() {
  check(srtp_init() == srtp_err_status_ok, "srtp_init");
  srtcp_known_answer();
  sha1_32_profile();
  check(srtp_shutdown() == srtp_err_status_ok, "srtp_shutdown");
  if (failures) return 1;
  std::puts("libsrtp C++ consumer: ok");
  return 0;
}
