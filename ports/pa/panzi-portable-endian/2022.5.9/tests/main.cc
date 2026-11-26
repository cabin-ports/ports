/* htobe*, htole*, be*toh and le*toh give the byte layout their names state,
   and round-trip. */
#include <portable_endian.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check_bytes(const void* p, const unsigned char* want, size_t n, int line)
{
    if (memcmp(p, want, n) != 0)
    {
        fprintf(stderr, "main.cc:%d: unexpected byte order\n", line);
        ++failures;
    }
}

static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "main.cc:%d: check failed\n", line);
        ++failures;
    }
}

int main(void)
{
    static const unsigned char be16[] = {0x12, 0x34}, le16[] = {0x34, 0x12};
    static const unsigned char be32[] = {1, 2, 3, 4}, le32[] = {4, 3, 2, 1};
    static const unsigned char be64[] = {1, 2, 3, 4, 5, 6, 7, 8},
                               le64[] = {8, 7, 6, 5, 4, 3, 2, 1};
    uint16_t v16 = 0x1234, t16;
    uint32_t v32 = 0x01020304u, t32;
    uint64_t v64 = 0x0102030405060708ULL, t64;
    uint16_t host16 = 1;

    t16 = htobe16(v16); check_bytes(&t16, be16, 2, __LINE__);
    t16 = htole16(v16); check_bytes(&t16, le16, 2, __LINE__);
    t32 = htobe32(v32); check_bytes(&t32, be32, 4, __LINE__);
    t32 = htole32(v32); check_bytes(&t32, le32, 4, __LINE__);
    t64 = htobe64(v64); check_bytes(&t64, be64, 8, __LINE__);
    t64 = htole64(v64); check_bytes(&t64, le64, 8, __LINE__);

    check(be16toh(htobe16(v16)) == v16, __LINE__);
    check(le16toh(htole16(v16)) == v16, __LINE__);
    check(be32toh(htobe32(v32)) == v32, __LINE__);
    check(le32toh(htole32(v32)) == v32, __LINE__);
    check(be64toh(htobe64(v64)) == v64, __LINE__);
    check(le64toh(htole64(v64)) == v64, __LINE__);

    /* Reading wire bytes back gives the original value. */
    memcpy(&t32, be32, 4); check(be32toh(t32) == v32, __LINE__);
    memcpy(&t64, le64, 8); check(le64toh(t64) == v64, __LINE__);

    /* The host's own order is the identity. */
    if (*reinterpret_cast<unsigned char*>(&host16) == 1)
    {
        check(htole16(v16) == v16 && htole32(v32) == v32 && htole64(v64) == v64, __LINE__);
    }
    else
    {
        check(htobe16(v16) == v16 && htobe32(v32) == v32 && htobe64(v64) == v64, __LINE__);
    }

    if (failures == 0) puts("ok");
    return failures != 0;
}
