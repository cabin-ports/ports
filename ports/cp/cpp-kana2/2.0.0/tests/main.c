#include <cpp-kana2/ext/c_api.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      printf("FAIL %s (line %d)\n", #cond, __LINE__);       \
      ++failures;                                           \
    }                                                       \
  } while (0)

int main(void) {
  CHECK(strcmp(cppk2_version(), "2.0.0") == 0);

  cppk2_romaji_options ro;
  cppk2_kana_options ko;
  cppk2_conversion *c = NULL;
  cppk2_error err;
  memset(&err, 0, sizeof(err));
  CHECK(cppk2_romaji_options_init(&ro, sizeof(ro)) == CPPK2_OK);
  CHECK(cppk2_kana_options_init(&ko, sizeof(ko)) == CPPK2_OK);

  /* Sokuon: doubled consonant. */
  CHECK(cppk2_kana_to_romaji("きって", &ro, &c, &err) == CPPK2_OK);
  CHECK(c != NULL && strcmp(c->rendered, "kitte") == 0);
  CHECK(c != NULL && c->mora_count == 3 && c->ok == 1);
  CHECK(c != NULL && c->moras[1].kind == CPPK2_MORA_SOKUON);
  cppk2_conversion_free(c);
  c = NULL;

  /* Long vowel mark repeats the vowel. */
  CHECK(cppk2_kana_to_romaji("コーヒー", &ro, &c, &err) == CPPK2_OK);
  CHECK(c != NULL && strcmp(c->rendered, "koohii") == 0);
  cppk2_conversion_free(c);
  c = NULL;

  CHECK(cppk2_romaji_to_kana("konnichiha", &ko, &c, &err) == CPPK2_OK);
  CHECK(c != NULL && strcmp(c->rendered, "こんにちは") == 0);
  cppk2_conversion_free(c);
  c = NULL;

  CHECK(cppk2_convert_script("ｶﾀｶﾅ", CPPK2_SCRIPT_HIRAGANA, CPPK2_UNKNOWN_KEEP, &c, &err) == CPPK2_OK);
  CHECK(c != NULL && strcmp(c->rendered, "かたかな") == 0);
  cppk2_conversion_free(c);
  c = NULL;

  /* A truncated UTF-8 sequence fails with a code and a detail the caller frees. */
  CHECK(cppk2_kana_to_romaji("\xE3\x81", NULL, &c, &err) == CPPK2_ERR_INVALID_UTF8);
  CHECK(c == NULL && err.code == CPPK2_ERR_INVALID_UTF8);
  CHECK(err.detail != NULL);
  cppk2_string_free(err.detail);

  if (failures != 0) return 1;
  puts("ok");
  return 0;
}
