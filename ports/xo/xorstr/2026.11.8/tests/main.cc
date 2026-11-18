#include <xorstr.hpp>

#include <cstdio>
#include <cstring>

namespace
{
int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}
} // namespace

int main()
{
    // xorstr_ returns a pointer into a temporary, so each use stays within
    // one full-expression.
    check(std::strcmp(xorstr_("an extra long hello_world"), "an extra long hello_world") == 0,
          "README.md's example decrypts");
    check(std::strcmp(xorstr_("short"), "short") == 0, "a short string decrypts");
    check(std::strcmp(xorstr_("0123456789abcde"), "0123456789abcde") == 0, "16 bytes with the terminator");
    check(std::strcmp(xorstr_("0123456789abcdef"), "0123456789abcdef") == 0, "17 bytes with the terminator");
    // The storage is uint64_t objects, so compare bytes rather than read
    // them as wchar_t.
    check(std::memcmp(xorstr_(L"a wide string over sixteen bytes"), L"a wide string over sixteen bytes",
                      sizeof(L"a wide string over sixteen bytes")) == 0,
          "a wide string decrypts");

    auto secret = xorstr("a secret over sixteen bytes");
    check(secret.size() == 27, "size() excludes the terminator");
    check(std::memcmp(secret.get(), "a secret over sixteen bytes", sizeof("a secret over sixteen bytes")) != 0, "the storage starts encrypted");
    secret.crypt();
    check(std::strcmp(secret.get(), "a secret over sixteen bytes") == 0, "crypt() decrypts in place");
    secret.crypt();
    check(std::memcmp(secret.get(), "a secret over sixteen bytes", sizeof("a secret over sixteen bytes")) != 0, "crypt() again re-encrypts");
    check(std::strcmp(secret.crypt_get(), "a secret over sixteen bytes") == 0, "crypt_get() decrypts");

    if (failures != 0)
    {
        return 1;
    }
    std::puts("xorstr consumer test passed");
    return 0;
}
