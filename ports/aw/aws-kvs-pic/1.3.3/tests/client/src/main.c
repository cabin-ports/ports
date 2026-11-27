#include <com/amazonaws/kinesis/video/client/Include.h>
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
    EXPECT(serviceCallResultCheck(SERVICE_CALL_RESULT_OK) == STATUS_SUCCESS);
    EXPECT(serviceCallResultCheck(SERVICE_CALL_RESOURCE_NOT_FOUND) == STATUS_SERVICE_CALL_RESOURCE_NOT_FOUND_ERROR);
    EXPECT(serviceCallResultCheck(SERVICE_CALL_INVALID_ARG) == STATUS_SERVICE_CALL_INVALID_ARG_ERROR);
    return 0;
}
