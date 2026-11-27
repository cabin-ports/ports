#define BUFFERSIZE 4096
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include <b64/decode.h>
#include <b64/encode.h>

static int failures;

static void check(const std::string& plain, const std::string& expected)
{
	std::stringstream in(plain);
	std::stringstream code;
	base64::encoder enc;
	enc.encode(in, code);
	if (code.str() != expected)
	{
		std::cout << "encode \"" << plain << "\": got \"" << code.str() << "\"\n";
		++failures;
	}

	std::stringstream back;
	base64::decoder dec;
	dec.decode(code, back);
	if (back.str() != plain)
	{
		std::cout << "decode \"" << expected << "\": got \"" << back.str() << "\"\n";
		++failures;
	}
}

int main()
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
