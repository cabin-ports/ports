/* The C consumer: SHA-256 against FIPS 180-2's examples, AES-GCM against
 * test case 4 of the GCM specification (McGrew and Viega) with a forged tag
 * and forged additional data rejected, an X.509 parse of upstream's
 * self-signed test certificate (framework/data_files/server5-selfsigned.crt)
 * with another host name and corrupted PEM rejected, and the verification
 * of upstream's server5.crt against the test EC CA that signed it
 * (test-ca2.crt), with a forged signature rejected and server5-expired.crt
 * reported expired. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <mbedtls/gcm.h>
#include <mbedtls/oid.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <mbedtls/x509_crt.h>

static int ok = 1;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

static int from_hex(const char *hex, unsigned char *out) {
    size_t n = strlen(hex) / 2, i;
    for (i = 0; i < n; ++i) {
        unsigned int byte;
        if (sscanf(hex + 2 * i, "%2x", &byte) != 1) {
            return -1;
        }
        out[i] = (unsigned char) byte;
    }
    return (int) n;
}

static int equals_hex(const unsigned char *bytes, size_t len, const char *hex) {
    unsigned char expected[128];
    return from_hex(hex, expected) == (int) len && memcmp(bytes, expected, len) == 0;
}

static void hashes(void) {
    static const char abc[] = "abc";
    static const char two_blocks[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static unsigned char a_million[1000000];
    unsigned char digest[32];
    mbedtls_sha256_context ctx;
    size_t at;

    check(mbedtls_sha256((const unsigned char *) abc, 3, digest, 0) == 0 &&
              equals_hex(digest, 32, "ba7816bf8f01cfea414140de5dae2223"
                                     "b00361a396177a9cb410ff61f20015ad"),
          "SHA-256 of \"abc\"");
    check(mbedtls_sha256((const unsigned char *) two_blocks, strlen(two_blocks), digest, 0) == 0 &&
              equals_hex(digest, 32, "248d6a61d20638b8e5c026930c3e6039"
                                     "a33ce45964ff2167f6ecedd419db06c1"),
          "SHA-256 of the 448-bit message");
    check(mbedtls_sha256((const unsigned char *) abc, 3, digest, 1) == 0 &&
              equals_hex(digest, 28, "23097d223405d8228642a477bda255b3"
                                     "2aadbce4bda0b3f7e36c9da7"),
          "SHA-224 of \"abc\"");

    /* A million 'a's, streamed in uneven pieces. */
    memset(a_million, 'a', sizeof a_million);
    mbedtls_sha256_init(&ctx);
    check(mbedtls_sha256_starts(&ctx, 0) == 0, "SHA-256 starts");
    for (at = 0; at < sizeof a_million; at += 999) {
        size_t n = sizeof a_million - at < 999 ? sizeof a_million - at : 999;
        check(mbedtls_sha256_update(&ctx, a_million + at, n) == 0, "SHA-256 update");
    }
    check(mbedtls_sha256_finish(&ctx, digest) == 0 &&
              equals_hex(digest, 32, "cdc76e5c9914fb9281a1c7e284d73e67"
                                     "f1809a48a497200e046d39ccc7112cd0"),
          "SHA-256 of a million 'a's, streamed");
    mbedtls_sha256_free(&ctx);
}

static void aes_gcm(void) {
    unsigned char key[16], iv[12], aad[20], plain[60], cipher[60], tag[16], out[60];
    unsigned char forged[16];
    mbedtls_gcm_context gcm;

    from_hex("feffe9928665731c6d6a8f9467308308", key);
    from_hex("cafebabefacedbaddecaf888", iv);
    from_hex("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad);
    from_hex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
             "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39", plain);

    mbedtls_gcm_init(&gcm);
    check(mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128) == 0, "GCM accepts a 128-bit AES key");
    check(mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, sizeof plain, iv, sizeof iv, aad, sizeof aad,
                                    plain, cipher, sizeof tag, tag) == 0,
          "GCM encrypts");
    check(equals_hex(cipher, sizeof cipher,
                     "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
                     "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091"),
          "GCM test case 4 ciphertext");
    check(equals_hex(tag, sizeof tag, "5bc94fbc3221a5db94fae95ae7121a47"), "GCM test case 4 tag");

    memset(out, 0, sizeof out);
    check(mbedtls_gcm_auth_decrypt(&gcm, sizeof cipher, iv, sizeof iv, aad, sizeof aad, tag, sizeof tag,
                                   cipher, out) == 0 &&
              memcmp(out, plain, sizeof plain) == 0,
          "GCM decrypts back to the plaintext");

    memcpy(forged, tag, sizeof tag);
    forged[15] ^= 1;
    check(mbedtls_gcm_auth_decrypt(&gcm, sizeof cipher, iv, sizeof iv, aad, sizeof aad, forged, sizeof forged,
                                   cipher, out) == MBEDTLS_ERR_GCM_AUTH_FAILED,
          "GCM rejects a forged tag");
    aad[0] ^= 1;
    check(mbedtls_gcm_auth_decrypt(&gcm, sizeof cipher, iv, sizeof iv, aad, sizeof aad, tag, sizeof tag,
                                   cipher, out) == MBEDTLS_ERR_GCM_AUTH_FAILED,
          "GCM rejects forged additional data");
    mbedtls_gcm_free(&gcm);
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

/* The exact result for a chain whose earliest expiry is `expires`, judged
 * by the host clock, so the test outlives the certificates. */
static int verified(int ret, uint32_t flags, time_t expires) {
    if (time(NULL) <= expires) {
        return ret == 0 && flags == 0;
    }
    return ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags == MBEDTLS_X509_BADCERT_EXPIRED;
}

static void x509(void) {
    mbedtls_x509_crt crt, corrupt;
    char text[256];
    uint32_t flags = 0;
    int ret;
    static char corrupted[sizeof certificate];

    mbedtls_x509_crt_init(&crt);
    check(mbedtls_x509_crt_parse(&crt, (const unsigned char *) certificate, sizeof certificate) == 0,
          "the PEM certificate parses");
    check(crt.version == 3, "the certificate is X.509 v3");
    check(mbedtls_x509_dn_gets(text, sizeof text, &crt.subject) > 0 &&
              strcmp(text, "CN=selfsigned, OU=testing, O=PolarSSL, C=NL") == 0,
          "the subject is CN=selfsigned, OU=testing, O=PolarSSL, C=NL");
    check(mbedtls_x509_dn_gets(text, sizeof text, &crt.issuer) > 0 &&
              strcmp(text, "CN=selfsigned, OU=testing, O=PolarSSL, C=NL") == 0,
          "the certificate is its own issuer");
    check(mbedtls_x509_serial_gets(text, sizeof text, &crt.serial) > 0 &&
              strcmp(text, "53:A2:CB:4B:12:4E:AD:83:7D:A8:94:B2") == 0,
          "the serial number");
    check(crt.valid_from.year == 2023 && crt.valid_from.mon == 5 && crt.valid_from.day == 9 &&
              crt.valid_from.hour == 2 && crt.valid_from.min == 49 && crt.valid_from.sec == 57,
          "valid from 2023-05-09 02:49:57");
    check(crt.valid_to.year == 2033 && crt.valid_to.mon == 5 && crt.valid_to.day == 6, "valid to 2033-05-06");
    check(MBEDTLS_OID_CMP(MBEDTLS_OID_ECDSA_SHA256, &crt.sig_oid) == 0, "signed with ECDSA and SHA-256");
    check(mbedtls_pk_get_type(&crt.pk) == MBEDTLS_PK_ECKEY && mbedtls_pk_get_bitlen(&crt.pk) == 256,
          "the subject key is a 256-bit EC key");
    check(mbedtls_x509_crt_has_ext_type(&crt, MBEDTLS_X509_EXT_BASIC_CONSTRAINTS) &&
              mbedtls_x509_crt_has_ext_type(&crt, MBEDTLS_X509_EXT_KEY_USAGE) &&
              !mbedtls_x509_crt_has_ext_type(&crt, MBEDTLS_X509_EXT_SUBJECT_ALT_NAME) &&
              mbedtls_x509_crt_check_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) == 0 &&
              mbedtls_x509_crt_check_key_usage(&crt, MBEDTLS_X509_KU_KEY_CERT_SIGN) != 0,
          "the key usage is digital signature only");

    ret = mbedtls_x509_crt_verify(&crt, &crt, NULL, "selfsigned", &flags, NULL, NULL);
    /* 2033-05-06 02:49:57 UTC */
    check(verified(ret, flags, (time_t) 1998960597), "the certificate verifies against itself as trusted");
    flags = 0;
    check(mbedtls_x509_crt_verify(&crt, &crt, NULL, "other", &flags, NULL, NULL) ==
                  MBEDTLS_ERR_X509_CERT_VERIFY_FAILED &&
              (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0,
          "a different host name is a mismatch");

    /* A character of the base64 body replaced by one outside its alphabet. */
    memcpy(corrupted, certificate, sizeof certificate);
    corrupted[100] = '*';
    mbedtls_x509_crt_init(&corrupt);
    check(mbedtls_x509_crt_parse(&corrupt, (const unsigned char *) corrupted, sizeof corrupted) != 0,
          "corrupted PEM does not parse");
    mbedtls_x509_crt_free(&corrupt);
    mbedtls_x509_crt_free(&crt);
}

static const char ca_certificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICBzCCAYugAwIBAgIJAMFD4n5iQ8zoMAwGCCqGSM49BAMCBQAwPjELMAkGA1UE\n"
    "BhMCTkwxETAPBgNVBAoMCFBvbGFyU1NMMRwwGgYDVQQDDBNQb2xhcnNzbCBUZXN0\n"
    "IEVDIENBMB4XDTE5MDIxMDE0NDQwMFoXDTI5MDIxMDE0NDQwMFowPjELMAkGA1UE\n"
    "BhMCTkwxETAPBgNVBAoMCFBvbGFyU1NMMRwwGgYDVQQDDBNQb2xhcnNzbCBUZXN0\n"
    "IEVDIENBMHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEw9orNEE3WC+HVv78ibopQ0tO\n"
    "4G7DDldTMzlY1FK0kZU5CyPfXxckYkj8GpUpziwth8KIUoCv1mqrId240xxuWLjK\n"
    "6LJpjvNBrSnDtF91p0dv1RkpVWmaUzsgtGYWYDMeo1MwUTAPBgNVHRMBAf8EBTAD\n"
    "AQH/MB0GA1UdDgQWBBSdbSAkSQE/K8t4tRm8fiTJ2/s2fDAfBgNVHSMEGDAWgBSd\n"
    "bSAkSQE/K8t4tRm8fiTJ2/s2fDAMBggqhkjOPQQDAgUAA2gAMGUCMQDpNWfBIlzq\n"
    "6xV2UwQD/1YGz9fQUM7AfNKzVa2PVBpf/QD1TAylTYTF4GI6qlb6EPYCMF/YVa29\n"
    "N5yC1mFAir19jb9Pl9iiIkRm17dM4y6m5VIMepEPm/VlWAa8H5p1+BPbGw==\n"
    "-----END CERTIFICATE-----\n";

static const char leaf_certificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICIDCCAaWgAwIBAgIBCTAKBggqhkjOPQQDAjA+MQswCQYDVQQGEwJOTDERMA8G\n"
    "A1UECgwIUG9sYXJTU0wxHDAaBgNVBAMME1BvbGFyc3NsIFRlc3QgRUMgQ0EwHhcN\n"
    "MjMwNTE3MDcxMDM2WhcNMzMwNTE0MDcxMDM2WjA0MQswCQYDVQQGEwJOTDERMA8G\n"
    "A1UECgwIUG9sYXJTU0wxEjAQBgNVBAMMCWxvY2FsaG9zdDBZMBMGByqGSM49AgEG\n"
    "CCqGSM49AwEHA0IABDfMVtl2CR5acj7HWS3/IG7ufPkGkXTQrRS192giWWKSTuUA\n"
    "2CMR/+ov0jRdXRa9iojCa3cNVc2KKg76Aci07f+jgZ0wgZowCQYDVR0TBAIwADAd\n"
    "BgNVHQ4EFgQUUGGlj9QH2deCAQzlZX+MY0anE74wbgYDVR0jBGcwZYAUnW0gJEkB\n"
    "PyvLeLUZvH4kydv7NnyhQqRAMD4xCzAJBgNVBAYTAk5MMREwDwYDVQQKDAhQb2xh\n"
    "clNTTDEcMBoGA1UEAwwTUG9sYXJzc2wgVGVzdCBFQyBDQYIJAMFD4n5iQ8zoMAoG\n"
    "CCqGSM49BAMCA2kAMGYCMQDg6p7PPfr2+n7nGvya3pU4ust3k7Obk4/tZX+uHHRQ\n"
    "qaccsyULeFNzkyRvWHFeT5sCMQCzDJX79Ii7hILYza/iXWJe/BjJEE8MteCRGXDN\n"
    "06jC+BLgOH1KQV9ArqEh3AhOhEg=\n"
    "-----END CERTIFICATE-----\n";

static const char expired_certificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICHjCCAaWgAwIBAgIBHjAKBggqhkjOPQQDAjA+MQswCQYDVQQGEwJOTDERMA8G\n"
    "A1UEChMIUG9sYXJTU0wxHDAaBgNVBAMTE1BvbGFyc3NsIFRlc3QgRUMgQ0EwHhcN\n"
    "MDQwMzEwMTIwOTMwWhcNMTQwMzA4MTIwOTMwWjA0MQswCQYDVQQGEwJOTDERMA8G\n"
    "A1UEChMIUG9sYXJTU0wxEjAQBgNVBAMTCWxvY2FsaG9zdDBZMBMGByqGSM49AgEG\n"
    "CCqGSM49AwEHA0IABDfMVtl2CR5acj7HWS3/IG7ufPkGkXTQrRS192giWWKSTuUA\n"
    "2CMR/+ov0jRdXRa9iojCa3cNVc2KKg76Aci07f+jgZ0wgZowCQYDVR0TBAIwADAd\n"
    "BgNVHQ4EFgQUUGGlj9QH2deCAQzlZX+MY0anE74wbgYDVR0jBGcwZYAUnW0gJEkB\n"
    "PyvLeLUZvH4kydv7NnyhQqRAMD4xCzAJBgNVBAYTAk5MMREwDwYDVQQKEwhQb2xh\n"
    "clNTTDEcMBoGA1UEAxMTUG9sYXJzc2wgVGVzdCBFQyBDQYIJAMFD4n5iQ8zoMAoG\n"
    "CCqGSM49BAMCA2cAMGQCMCDxvDmhlrEk0r4hqCwvQDxWEoXPbbD1gglfLT3BsGpu\n"
    "XHUQ1W2HwB3o/7N5I13BBgIwcmG17zyNIOkYiyExYtPCZCpbofEMpRY5qWG0K6YL\n"
    "fN08jSzyFt6kbO4ak0D6tC5Q\n"
    "-----END CERTIFICATE-----\n";

static void chain(void) {
    mbedtls_x509_crt ca, leaf, forged, expired;
    unsigned char der[1024];
    size_t der_len;
    uint32_t flags = 0;
    int ret;

    mbedtls_x509_crt_init(&ca);
    mbedtls_x509_crt_init(&leaf);
    mbedtls_x509_crt_init(&forged);
    mbedtls_x509_crt_init(&expired);
    if (mbedtls_x509_crt_parse(&ca, (const unsigned char *) ca_certificate, sizeof ca_certificate) != 0 ||
        mbedtls_x509_crt_parse(&leaf, (const unsigned char *) leaf_certificate, sizeof leaf_certificate) != 0) {
        check(0, "the CA and the certificate it issued parse");
        goto done;
    }
    ret = mbedtls_x509_crt_verify(&leaf, &ca, NULL, "localhost", &flags, NULL, NULL);
    /* the CA's expiry, 2029-02-10 14:44:00 UTC */
    check(verified(ret, flags, (time_t) 1865429040), "the certificate verifies against its CA");

    /* The certificate with one bit of its signature flipped. */
    der_len = leaf.raw.len;
    if (der_len == 0 || der_len > sizeof der) {
        check(0, "the DER fits");
        goto done;
    }
    memcpy(der, leaf.raw.p, der_len);
    der[der_len - 1] ^= 1;
    check(mbedtls_x509_crt_parse_der(&forged, der, der_len) == 0, "the forged certificate still parses");
    flags = 0;
    check(mbedtls_x509_crt_verify(&forged, &ca, NULL, "localhost", &flags, NULL, NULL) ==
                  MBEDTLS_ERR_X509_CERT_VERIFY_FAILED &&
              (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) != 0,
          "a forged signature is not trusted");

    flags = 0;
    check(mbedtls_x509_crt_parse(&expired, (const unsigned char *) expired_certificate,
                                 sizeof expired_certificate) == 0 &&
              mbedtls_x509_crt_verify(&expired, &ca, NULL, "localhost", &flags, NULL, NULL) ==
                  MBEDTLS_ERR_X509_CERT_VERIFY_FAILED &&
              flags == MBEDTLS_X509_BADCERT_EXPIRED,
          "a certificate that expired in 2014 is reported expired, and only that");
done:
    mbedtls_x509_crt_free(&expired);
    mbedtls_x509_crt_free(&forged);
    mbedtls_x509_crt_free(&leaf);
    mbedtls_x509_crt_free(&ca);
}

int main(void) {
    char version[18];
    mbedtls_version_get_string(version);
    check(MBEDTLS_VERSION_NUMBER == 0x03060700 && strcmp(version, "3.6.7") == 0, "the library is 3.6.7");
    hashes();
    aes_gcm();
    x509();
    chain();
    if (!ok) {
        fprintf(stderr, "mbedtls (C): FAILED\n");
        return 1;
    }
    printf("mbedtls (C): ok\n");
    return 0;
}
