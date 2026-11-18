#include <stringzilla/stringzilla.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static int same_hex(sz_u8_t const *bytes, size_t length, char const *hex) {
  static char const digits[] = "0123456789abcdef";
  size_t i;
  for (i = 0; i < length; ++i)
    if (digits[bytes[i] >> 4] != hex[2 * i] || digits[bytes[i] & 15] != hex[2 * i + 1])
      return 0;
  return hex[2 * length] == '\0';
}

static void check_find(void) {
  char const *text = "hello world, hello stringzilla";
  sz_size_t length = (sz_size_t)strlen(text);
  sz_byteset_t vowels;

  CHECK(sz_find(text, length, "hello", 5) == text);
  CHECK(sz_rfind(text, length, "hello", 5) == text + 13);
  CHECK(sz_find(text, length, "zilla", 5) == text + 25);
  CHECK(sz_find(text, length, "xyz", 3) == SZ_NULL);
  CHECK(sz_find(text, length, "hello s", 7) == text + 13);
  CHECK(sz_find_byte(text, length, "w") == text + 6);
  CHECK(sz_rfind_byte(text, length, "o") == text + 17);

  sz_byteset_init(&vowels);
  sz_byteset_add(&vowels, 'i');
  sz_byteset_add(&vowels, 'w');
  CHECK(sz_find_byteset(text, length, &vowels) == text + 6);
  CHECK(sz_rfind_byteset(text, length, &vowels) == text + 26);
}

static void check_compare(void) {
  CHECK(sz_equal("hello", "hello", 5) == sz_true_k);
  CHECK(sz_equal("hello", "help!", 5) == sz_false_k);
  CHECK(sz_order("apple", 5, "apricot", 7) == sz_less_k);
  CHECK(sz_order("abc", 3, "ab", 2) == sz_greater_k);
  CHECK(sz_order("same", 4, "same", 4) == sz_equal_k);
}

static void check_memory(void) {
  char buffer[16];
  sz_fill(buffer, sizeof(buffer), 'x');
  CHECK(buffer[0] == 'x' && buffer[15] == 'x');
  sz_copy(buffer + 2, "abcdef", 6);
  CHECK(memcmp(buffer, "xxabcdefxx", 10) == 0);
  sz_move(buffer + 1, buffer + 2, 6);
  CHECK(memcmp(buffer, "xabcdeffxx", 10) == 0);
  sz_move(buffer + 2, buffer + 1, 6);
  CHECK(memcmp(buffer, "xaabcdefxx", 10) == 0);
}

/* FIPS 180-2 known answers and padding and block boundaries, fed in chunks. */
static void check_sha256(void) {
  static char const abc56[] =
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  static char const abc112[] =
      "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
      "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
  char chunk[1000];
  sz_u8_t digest[SZ_SHA256_DIGEST_LENGTH];
  sz_sha256_state_t state;
  int i;

  sz_sha256_state_init(&state);
  sz_sha256_state_digest(&state, digest);
  CHECK(same_hex(digest, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

  sz_sha256_state_init(&state);
  sz_sha256_state_update(&state, "abc", 3);
  sz_sha256_state_digest(&state, digest);
  CHECK(same_hex(digest, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

  sz_sha256_state_init(&state);
  sz_sha256_state_update(&state, abc56, 56);
  sz_sha256_state_digest(&state, digest);
  CHECK(same_hex(digest, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

  memset(chunk, 'a', sizeof(chunk));
  sz_sha256_state_init(&state);
  sz_sha256_state_update(&state, chunk, 55);
  sz_sha256_state_digest(&state, digest);
  CHECK(same_hex(digest, 32, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
  sz_sha256_state_init(&state);
  sz_sha256_state_update(&state, abc112, 1);
  sz_sha256_state_update(&state, abc112 + 1, 111);
  sz_sha256_state_digest(&state, digest);
  CHECK(same_hex(digest, 32, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
  sz_sha256_state_init(&state);
  for (i = 0; i < 1000; ++i) sz_sha256_state_update(&state, chunk, sizeof(chunk));
  sz_sha256_state_digest(&state, digest);
  CHECK(same_hex(digest, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

static void check_hash(void) {
  char const *text = "The quick brown fox jumps over the lazy dog";
  sz_size_t length = (sz_size_t)strlen(text);
  sz_hash_state_t state;
  sz_u64_t whole = sz_hash(text, length, 42);

  sz_hash_state_init(&state, 42);
  sz_hash_state_update(&state, text, 10);
  sz_hash_state_update(&state, text + 10, length - 10);
  CHECK(sz_hash_state_digest(&state) == whole);
  CHECK(sz_hash(text, length, 42) == whole);
  CHECK(sz_hash(text, length, 43) != whole);
  CHECK(sz_bytesum("hello", 5) == 532);
  {
    char longer[150];
    int i;
    for (i = 0; i < 150; ++i) longer[i] = (char)('a' + i % 26);
    sz_hash_state_init(&state, 42);
    sz_hash_state_update(&state, longer, 10);
    sz_hash_state_update(&state, longer + 10, 140);
    CHECK(sz_hash_state_digest(&state) == sz_hash(longer, 150, 42));
    /* hash.h:136 promises the same output on every platform and backend. */
    CHECK(whole == 0xd35454799a16558full);
    CHECK(sz_hash(longer, 150, 42) == 0x2db126aaaef1a75dull);
  }
}

/* AES-256-GCM: test case 14 of McGrew and Viega's GCM specification, and
   messages checked against another implementation. */
static void check_aes_gcm(void) {
  sz_u8_t secret[32], nonce[12], tag[16];
  char zeros[16], output[16], back[16];
  sz_aes256_gcm_key_t key;
  int i;

  memset(secret, 0, sizeof(secret));
  memset(nonce, 0, sizeof(nonce));
  memset(zeros, 0, sizeof(zeros));
  sz_aes256_gcm_key_init(&key, secret);
  sz_aes256_gcm_encrypt(&key, nonce, SZ_NULL, 0, zeros, 16, output, tag);
  CHECK(same_hex((sz_u8_t const *)output, 16, "cea7403d4d606b6e074ec5d3baf39d18"));
  CHECK(same_hex(tag, 16, "d0d1c8a799996bf0265b98b5d48ab919"));

  for (i = 0; i < 32; ++i) secret[i] = (sz_u8_t)i;
  for (i = 0; i < 12; ++i) nonce[i] = (sz_u8_t)i;
  sz_aes256_gcm_key_init(&key, secret);
  sz_aes256_gcm_encrypt(&key, nonce, "hdr", 3, "StringZilla", 11, output, tag);
  CHECK(same_hex((sz_u8_t const *)output, 11, "1476a472ab829872e12df6"));
  CHECK(same_hex(tag, 16, "eba74979aa441e1a0d869f79cc497cf4"));
  CHECK(sz_aes256_gcm_decrypt(&key, nonce, "hdr", 3, output, 11, back, tag) == sz_success_k);
  CHECK(memcmp(back, "StringZilla", 11) == 0);
  tag[0] ^= 1;
  CHECK(sz_aes256_gcm_decrypt(&key, nonce, "hdr", 3, output, 11, back, tag) != sz_success_k);
  CHECK(back[0] == 0 && back[10] == 0);
  tag[0] ^= 1;
  tag[15] ^= 0x80;
  CHECK(sz_aes256_gcm_decrypt(&key, nonce, "hdr", 3, output, 11, back, tag) != sz_success_k);
  { /* 128 bytes, as text and as associated data, reach the eight-block GHASH. */
    char plain[128], sealed[128];
    for (i = 0; i < 128; ++i) plain[i] = (char)i;
    sz_aes256_gcm_encrypt(&key, nonce, SZ_NULL, 0, plain, 128, sealed, tag);
    CHECK(same_hex((sz_u8_t const *)sealed, 4, "4703d418"));
    CHECK(same_hex(tag, 16, "b966bcd0bc16acda8eaab22405d25bf9"));
    sz_aes256_gcm_encrypt(&key, nonce, plain, 128, SZ_NULL, 0, sealed, tag);
    CHECK(same_hex(tag, 16, "9e4addf96460ab38065670119637aa96"));
  }
}

/* The Unicode 17.0 tables: normalization, full case folding, and counting. */
static void check_unicode(void) {
  char output[64];
  sz_size_t written;
  sz_size_t matched = 0;
  sz_utf8_uncased_needle_metadata_t metadata;
  char const *haystack = "Die Stra\xC3\x9F" "e ist lang";
  sz_cptr_t found;

  CHECK(sz_utf8_count("h\xC3\xA9llo", 6) == 5);
  CHECK(sz_utf8_count("h\xC3\xA9llo, \xE2\x82\xAC 5 \xF0\x9F\x98\x80!", 19) == 13);

  written = sz_utf8_norm("\xC3\xA9", 2, sz_normal_form_nfd_k, output);
  CHECK(written == 3 && memcmp(output, "e\xCC\x81", 3) == 0);
  written = sz_utf8_norm("e\xCC\x81", 3, sz_normal_form_nfc_k, output);
  CHECK(written == 2 && memcmp(output, "\xC3\xA9", 2) == 0);
  written = sz_utf8_norm("\xEF\xAC\x81", 3, sz_normal_form_nfkc_k, output);
  CHECK(written == 2 && memcmp(output, "fi", 2) == 0);

  written = sz_utf8_uncased_fold("HELLO Stra\xC3\x9F" "e", 13, output);
  CHECK(written == 13 && memcmp(output, "hello strasse", 13) == 0);
  written = sz_utf8_uncased_fold("\xCE\x90", 2, output);
  CHECK(written == 6 && memcmp(output, "\xCE\xB9\xCC\x88\xCC\x81", 6) == 0);
  CHECK(sz_utf8_uncased_order("STRASSE", 7, "stra\xC3\x9F" "e", 7) == sz_equal_k);
  { /* 64 bytes reach the SIMD fold kernels. */
    static char const shout[] = "STRA\xC3\x9F" "E STRA\xC3\x9F" "E STRA\xC3\x9F" "E STRA\xC3\x9F" "E "
                                "STRA\xC3\x9F" "E STRA\xC3\x9F" "E STRA\xC3\x9F" "E STRA\xC3\x9F" "E ";
    char folded[3 * sizeof(shout)];
    written = sz_utf8_uncased_fold(shout, sizeof(shout) - 1, folded);
    CHECK(written == 64 && memcmp(folded, "strasse strasse strasse strasse strasse strasse strasse strasse ", 64) == 0);
  }

  memset(&metadata, 0, sizeof(metadata));
  found = sz_utf8_uncased_search(haystack, (sz_size_t)strlen(haystack), "STRASSE", 7,
                                 &metadata, &matched);
  CHECK(found == haystack + 4 && matched == 7);
  {
    char const *upper = "DIE STRA\xC3\x9F" "E IST LANG";
    memset(&metadata, 0, sizeof(metadata));
    found = sz_utf8_uncased_search(upper, (sz_size_t)strlen(upper), "strasse", 7, &metadata, &matched);
    CHECK(found == upper + 4 && matched == 7);
  }
}

int main(void) {
  CHECK(sz_version_major() == 5 && sz_version_minor() == 3 && sz_version_patch() == 2);
  check_find();
  check_compare();
  check_memory();
  check_sha256();
  check_hash();
  check_aes_gcm();
  check_unicode();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("stringzilla c ok");
  return 0;
}
