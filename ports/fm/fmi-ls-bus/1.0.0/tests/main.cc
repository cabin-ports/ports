// Uses the FMI-LS-BUS headers from C++11: writes one of each remaining CAN
// operation kind (CAN FD and CAN XL transmit, arbitration lost, bus error,
// status, wakeup, arbitration-lost configuration) and a format error into a
// std::vector, then dispatches them back by op code.  The operation structs'
// flexible array members are a compiler extension in C++.
#include <fmi3LsBusUtilCan.h>

#include <cstdio>
#include <vector>

static_assert(sizeof(fmi3LsBusCanOperationCanXlTransmit) == 22, "packed CAN XL header");
static_assert(sizeof(fmi3LsBusCanOperationBusError) == 15, "packed bus error");

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "fmi-ls-bus C++ test failed: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    std::vector<fmi3UInt8> buffer(256);
    fmi3LsBusUtilBufferInfo tx;
    FMI3_LS_BUS_BUFFER_INFO_INIT(&tx, buffer.data(), buffer.size());

    std::vector<fmi3UInt8> fd_data(12), xl_data(20);
    for (std::size_t i = 0; i < xl_data.size(); ++i) {
        xl_data[i] = static_cast<fmi3UInt8>(0xA0 + i);
        if (i < fd_data.size())
            fd_data[i] = static_cast<fmi3UInt8>(i);
    }
    const fmi3UInt8 bad[4] = {1, 2, 3, 4};

    FMI3_LS_BUS_CAN_CREATE_OP_CAN_FD_TRANSMIT(&tx, 0x18DAF110u, 1, 0, 1, fd_data.size(),
                                              fd_data.data());
    check(tx.status == fmi3True, "CAN FD transmit");
    FMI3_LS_BUS_CAN_CREATE_OP_CAN_XL_TRANSMIT(&tx, 0x7FFu, 0, 1, 0x03, 7, 0xCAFEF00Du,
                                              xl_data.size(), xl_data.data());
    check(tx.status == fmi3True, "CAN XL transmit");
    FMI3_LS_BUS_CAN_CREATE_OP_ARBITRATION_LOST(&tx, 0x100u);
    FMI3_LS_BUS_CAN_CREATE_OP_BUS_ERROR(&tx, 0x101u, FMI3_LS_BUS_CAN_BUSERROR_PARAM_ERROR_CODE_CRC_ERROR,
                                        FMI3_LS_BUS_CAN_BUSERROR_PARAM_ERROR_FLAG_SECONDARY_ERROR_FLAG,
                                        FMI3_LS_BUS_TRUE);
    FMI3_LS_BUS_CAN_CREATE_OP_STATUS(&tx, FMI3_LS_BUS_CAN_STATUS_PARAM_STATUS_KIND_ERROR_PASSIVE);
    FMI3_LS_BUS_CAN_CREATE_OP_WAKEUP(&tx);
    FMI3_LS_BUS_CAN_CREATE_OP_CONFIGURATION_ARBITRATION_LOST_BEHAVIOR(
        &tx, FMI3_LS_BUS_CAN_CONFIG_PARAM_ARBITRATION_LOST_BEHAVIOR_DISCARD_AND_NOTIFY);
    FMI3_LS_BUS_CREATE_OP_FORMAT_ERROR(&tx, sizeof bad, bad);
    check(tx.status == fmi3True, "the last operation fits");

    // 17 + 12, 22 + 20, 12, 15, 9, 8, 10, and 10 + 4 bytes.
    const std::size_t length = static_cast<std::size_t>(FMI3_LS_BUS_BUFFER_LENGTH(&tx));
    check(length == 29 + 42 + 12 + 15 + 9 + 8 + 10 + 14, "total length");

    const fmi3UInt8* bytes = buffer.data();
    std::size_t read_pos = 0;
    fmi3LsBusOperationHeader* op = nullptr;
    std::vector<fmi3LsBusOperationCode> codes;
    while (FMI3_LS_BUS_READ_NEXT_OPERATION_DIRECT(bytes, length, read_pos, op)) {
        codes.push_back(fmi3LsBusOperationCode(op->opCode));  // a copy, not a reference to a packed field
        switch (op->opCode) {
        case FMI3_LS_BUS_CAN_OP_CANFD_TRANSMIT: {
            auto t = reinterpret_cast<const fmi3LsBusCanOperationCanFdTransmit*>(op);
            check(t->id == 0x18DAF110u && t->ide == 1 && t->brs == 0 && t->esi == 1 &&
                      t->dataLength == 12 && t->data[11] == 11,
                  "CAN FD fields");
            break;
        }
        case FMI3_LS_BUS_CAN_OP_CANXL_TRANSMIT: {
            auto t = reinterpret_cast<const fmi3LsBusCanOperationCanXlTransmit*>(op);
            check(t->id == 0x7FFu && t->ide == 0 && t->sec == 1 && t->sdt == 0x03 &&
                      t->vcid == 7 &&
                      t->af == 0xCAFEF00Du && t->dataLength == 20 && t->data[0] == 0xA0 &&
                      t->data[19] == 0xB3,
                  "CAN XL fields");
            break;
        }
        case FMI3_LS_BUS_CAN_OP_ARBITRATION_LOST:
            check(reinterpret_cast<const fmi3LsBusCanOperationArbitrationLost*>(op)->id == 0x100u,
                  "arbitration lost ID");
            break;
        case FMI3_LS_BUS_CAN_OP_BUS_ERROR: {
            auto e = reinterpret_cast<const fmi3LsBusCanOperationBusError*>(op);
            check(e->id == 0x101u && e->errorCode == FMI3_LS_BUS_CAN_BUSERROR_PARAM_ERROR_CODE_CRC_ERROR &&
                      e->errorFlag == FMI3_LS_BUS_CAN_BUSERROR_PARAM_ERROR_FLAG_SECONDARY_ERROR_FLAG &&
                      e->isSender == FMI3_LS_BUS_TRUE,
                  "bus error fields");
            break;
        }
        case FMI3_LS_BUS_CAN_OP_STATUS:
            check(reinterpret_cast<const fmi3LsBusCanOperationStatus*>(op)->status ==
                      FMI3_LS_BUS_CAN_STATUS_PARAM_STATUS_KIND_ERROR_PASSIVE,
                  "status kind");
            break;
        case FMI3_LS_BUS_CAN_OP_WAKEUP:
            check(op->length == 8, "wakeup is a bare header");
            break;
        case FMI3_LS_BUS_CAN_OP_CONFIGURATION: {
            auto c = reinterpret_cast<const fmi3LsBusCanOperationConfiguration*>(op);
            check(c->header.length == 10 &&
                      c->parameterType == FMI3_LS_BUS_CAN_CONFIG_PARAM_TYPE_ARBITRATION_LOST_BEHAVIOR &&
                      c->arbitrationLostBehavior ==
                          FMI3_LS_BUS_CAN_CONFIG_PARAM_ARBITRATION_LOST_BEHAVIOR_DISCARD_AND_NOTIFY,
                  "arbitration-lost configuration");
            break;
        }
        case FMI3_LS_BUS_OP_FORMAT_ERROR: {
            auto f = reinterpret_cast<const fmi3LsBusOperationFormatError*>(op);
            check(f->dataLength == 4 && f->data[0] == 1 && f->data[3] == 4, "format error data");
            break;
        }
        default:
            check(false, "an unexpected operation");
        }
    }

    const std::vector<fmi3LsBusOperationCode> expected = {
        FMI3_LS_BUS_CAN_OP_CANFD_TRANSMIT, FMI3_LS_BUS_CAN_OP_CANXL_TRANSMIT,
        FMI3_LS_BUS_CAN_OP_ARBITRATION_LOST, FMI3_LS_BUS_CAN_OP_BUS_ERROR,
        FMI3_LS_BUS_CAN_OP_STATUS, FMI3_LS_BUS_CAN_OP_WAKEUP,
        FMI3_LS_BUS_CAN_OP_CONFIGURATION, FMI3_LS_BUS_OP_FORMAT_ERROR};
    check(codes == expected && read_pos == length, "operations come back in order");

    if (failures != 0)
        return 1;
    std::puts("fmi-ls-bus C++ ok");
    return 0;
}
