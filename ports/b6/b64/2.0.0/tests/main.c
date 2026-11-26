#include <stdio.h>
#include <string.h>

#include <b64/cdecode.h>
#include <b64/cencode.h>

static int failures;

static void check(const char* plain, const char* expected)
{
	char code[64];
	char back[64];
	size_t plainlen = strlen(plain);
	size_t n;
	size_t m;
	base64_encodestate es;
	base64_decodestate ds;

	base64_init_encodestate(&es);
	n = base64_encode_block(plain, plainlen, code, &es);
	n += base64_encode_blockend(code + n, &es);
	code[n] = '\0';
	if (strcmp(code, expected) != 0)
	{
		printf("encode \"%s\": got \"%s\", want \"%s\"\n", plain, code, expected);
		++failures;
	}

	base64_init_decodestate(&ds);
	m = base64_decode_block(code, n, back, &ds);
	back[m] = '\0';
	if (m != plainlen || strcmp(back, plain) != 0)
	{
		printf("decode \"%s\": got \"%s\"\n", code, back);
		++failures;
	}
}

int main(void)
{
	check("", "");
	check("f", "Zg==");
	check("fo", "Zm8=");
	check("foo", "Zm9v");
	check("foob", "Zm9vYg==");
	check("fooba", "Zm9vYmE=");
	check("foobar", "Zm9vYmFy");
	return failures != 0;
}
