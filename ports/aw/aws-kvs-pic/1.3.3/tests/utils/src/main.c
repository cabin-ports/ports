#include <com/amazonaws/kinesis/video/utils/Include.h>
#include <stdio.h>

#define EXPECT(c)                                                                                                                                    \
    do {                                                                                                                                             \
        if (!(c)) {                                                                                                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                                                                              \
            return 1;                                                                                                                                \
        }                                                                                                                                            \
    } while (0)

int main(void)
{
    EXPECT(COMPUTE_CRC32((PBYTE) "123456789", 9) == 0xCBF43926);
    return 0;
}
