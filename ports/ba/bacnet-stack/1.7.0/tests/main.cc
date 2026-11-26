#include <stdio.h>
#include <string.h>

#include "bacnet/bacdcode.h"
#include "bacnet/basic/sys/mstimer.h"

static int failures;

#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                             \
        }                                                           \
    } while (0)

int main(void)
{
    uint8_t buf[16];
    const uint8_t unsigned_1234[] = { 0x22, 0x04, 0xD2 };
    const uint8_t real_1[] = { 0x44, 0x3F, 0x80, 0x00, 0x00 };
    BACNET_UNSIGNED_INTEGER value = 0;
    struct mstimer timer;

    CHECK(encode_application_unsigned(buf, 1234) == 3);
    CHECK(memcmp(buf, unsigned_1234, 3) == 0);
    CHECK(bacnet_unsigned_application_decode(buf, 3, &value) == 3);
    CHECK(value == 1234);

    CHECK(encode_application_real(buf, 1.0f) == 5);
    CHECK(memcmp(buf, real_1, 5) == 0);

    /* mstimer_now comes from the per-OS port sources. */
    mstimer_init();
    (void)mstimer_now();
    mstimer_set(&timer, 60000);
    CHECK(mstimer_interval(&timer) == 60000);
    CHECK(!mstimer_expired(&timer));

    return failures ? 1 : 0;
}
