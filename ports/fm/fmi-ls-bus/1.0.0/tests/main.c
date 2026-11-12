/* Serializes CAN operations into a buffer with the FMI-LS-BUS utility
   macros, as an FMU fills its Tx binary variable, and parses them back with
   FMI3_LS_BUS_READ_NEXT_OPERATION and its _DIRECT form, as a receiving FMU
   does: the wire layout of a CAN transmit operation, every field read back
   through the packed operation structs, a full buffer refusing an
   operation, and a truncated one yielding nothing.  C11, the interface
   standard, so the headers' static_assert size checks are compiled. */
#include <fmi3LsBusUtilCan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if FMI3_LS_BUS_CHECK_OPERATION_SIZE != 1
#error "the headers' size checks are off under C11"
#endif

static int failures = 0;

static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "fmi-ls-bus C test failed: %s\n", what);
        ++failures;
    }
}

static fmi3UInt32 read_u32(const fmi3UInt8* p) {
    fmi3UInt32 value;
    memcpy(&value, p, sizeof value);
    return value;
}

static fmi3UInt16 read_u16(const fmi3UInt8* p) {
    fmi3UInt16 value;
    memcpy(&value, p, sizeof value);
    return value;
}

enum { CAPACITY = 64 };

int main(void) {
    /* Allocated, not declared, arrays: the macros read the bytes through
       operation struct pointers, which C's effective-type rules allow only
       for storage without a declared type. */
    fmi3UInt8* buffer = (fmi3UInt8*)malloc(CAPACITY);
    fmi3UInt8* copy = (fmi3UInt8*)malloc(CAPACITY);
    const fmi3UInt8 payload[3] = {0xDE, 0xAD, 0xBE};
    const fmi3UInt8 large[64] = {0};
    fmi3LsBusUtilBufferInfo tx;
    fmi3LsBusUtilBufferInfo rx;
    fmi3LsBusOperationHeader* op = NULL;
    fmi3Boolean more;
    size_t length;
    size_t read_pos = 0;
    int seen = 0;

    if (buffer == NULL || copy == NULL) {
        fprintf(stderr, "fmi-ls-bus C test failed: allocation\n");
        return 1;
    }
    FMI3_LS_BUS_BUFFER_INFO_INIT(&tx, buffer, CAPACITY);
    check(FMI3_LS_BUS_BUFFER_IS_EMPTY(&tx) == fmi3True, "a new buffer is empty");

    FMI3_LS_BUS_CAN_CREATE_OP_CAN_TRANSMIT(&tx, 0x123, FMI3_LS_BUS_TRUE, FMI3_LS_BUS_FALSE,
                                           sizeof payload, payload);
    check(tx.status == fmi3True, "a CAN transmit operation fits");
    FMI3_LS_BUS_CAN_CREATE_OP_CONFIRM(&tx, 0x123);
    check(tx.status == fmi3True, "a confirm operation fits");
    FMI3_LS_BUS_CAN_CREATE_OP_CONFIGURATION_CAN_BAUDRATE(&tx, 500000);
    check(tx.status == fmi3True, "a baud-rate configuration fits");
    length = (size_t)FMI3_LS_BUS_BUFFER_LENGTH(&tx);
    check(length == 19 + 12 + 13, "operations of 16 + 3, 12, and 13 bytes");

    /* 20 bytes remain, so a transmit operation with 64 data bytes is refused
       and nothing is written. */
    FMI3_LS_BUS_CAN_CREATE_OP_CAN_TRANSMIT(&tx, 0x124, 0, 0, sizeof large, large);
    check(tx.status == fmi3False && (size_t)FMI3_LS_BUS_BUFFER_LENGTH(&tx) == length,
          "a full buffer refuses an operation");

    /* Header (op code, length), ID, IDE, RTR, data length, data. */
    check(read_u32(buffer) == FMI3_LS_BUS_CAN_OP_CAN_TRANSMIT && read_u32(buffer + 4) == 19 &&
              read_u32(buffer + 8) == 0x123 && buffer[12] == 1 && buffer[13] == 0 &&
              read_u16(buffer + 14) == 3 && memcmp(buffer + 16, payload, 3) == 0,
          "the CAN transmit operation's layout");

    while (FMI3_LS_BUS_READ_NEXT_OPERATION(&tx, op)) {
        ++seen;
        switch (op->opCode) {
        case FMI3_LS_BUS_CAN_OP_CAN_TRANSMIT: {
            const fmi3LsBusCanOperationCanTransmit* t = (const fmi3LsBusCanOperationCanTransmit*)op;
            check(seen == 1 && t->header.length == 19 && t->id == 0x123 && t->ide == 1 &&
                      t->rtr == 0 && t->dataLength == 3 && t->data[0] == 0xDE &&
                      t->data[1] == 0xAD && t->data[2] == 0xBE,
                  "CAN transmit fields");
            break;
        }
        case FMI3_LS_BUS_CAN_OP_CONFIRM:
            check(seen == 2 && ((const fmi3LsBusCanOperationConfirm*)op)->id == 0x123,
                  "confirm fields");
            break;
        case FMI3_LS_BUS_CAN_OP_CONFIGURATION: {
            const fmi3LsBusCanOperationConfiguration* c = (const fmi3LsBusCanOperationConfiguration*)op;
            check(seen == 3 && c->header.length == 13 &&
                      c->parameterType == FMI3_LS_BUS_CAN_CONFIG_PARAM_TYPE_CAN_BAUDRATE &&
                      c->baudrate == 500000,
                  "configuration fields");
            break;
        }
        default:
            check(0, "an unexpected operation");
        }
    }
    check(seen == 3, "three operations are read");

    /* An importer receives the bytes as an fmi3Binary value. */
    memcpy(copy, buffer, length);
    seen = 0;
    while (FMI3_LS_BUS_READ_NEXT_OPERATION_DIRECT(copy, length, read_pos, op))
        ++seen;
    check(seen == 3 && read_pos == length, "reading a copy of the buffer directly");

    /* The macro is not parenthesized, so its result is stored before use. */
    FMI3_LS_BUS_BUFFER_INFO_INIT(&rx, copy, CAPACITY);
    FMI3_LS_BUS_BUFFER_WRITE(&rx, buffer, 10);
    more = FMI3_LS_BUS_READ_NEXT_OPERATION(&rx, op);
    check(rx.status == fmi3True && more == fmi3False, "a truncated operation is not read");

    FMI3_LS_BUS_BUFFER_INFO_RESET(&tx);
    check(FMI3_LS_BUS_BUFFER_IS_EMPTY(&tx) == fmi3True, "a reset buffer is empty");

    free(buffer);
    free(copy);
    if (failures != 0)
        return 1;
    puts("fmi-ls-bus C ok");
    return 0;
}
