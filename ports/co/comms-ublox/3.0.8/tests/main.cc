// Frames two u-blox UBX messages (MON-VER poll, MON-RXR) through UbloxFrame,
// checks the exact bytes including sync, length and Fletcher checksum, reads
// them back, and checks that corrupted input is rejected.
#include <cc_ublox/Message.h>
#include <cc_ublox/frame/UbloxFrame.h>
#include <cc_ublox/message/MonRxr.h>
#include <cc_ublox/message/MonVerPoll.h>

#include <comms/comms.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <tuple>

namespace {

using Msg = cc_ublox::Message<
    comms::option::app::ReadIterator<const std::uint8_t *>,
    comms::option::app::WriteIterator<std::uint8_t *>,
    comms::option::app::IdInfoInterface,
    comms::option::app::LengthInfoInterface>;

using MonVerPoll = cc_ublox::message::MonVerPoll<Msg>;
using MonRxr = cc_ublox::message::MonRxr<Msg>;
using Frame = cc_ublox::frame::UbloxFrame<
    Msg, std::tuple<MonVerPoll, MonRxr>>;

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool same(const std::uint8_t *a, const std::uint8_t *b, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    Frame frame;
    std::uint8_t buf[32] = {};

    // MON-VER poll: sync B5 62, id 0A 04, empty payload, checksum 0E 34.
    const std::uint8_t poll[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
    MonVerPoll pollMsg;
    std::uint8_t *w = buf;
    check(frame.write(pollMsg, w, sizeof(buf)) == comms::ErrorStatus::Success,
          "write poll");
    check(static_cast<std::size_t>(w - buf) == sizeof(poll), "poll length");
    check(same(buf, poll, sizeof(poll)), "poll bytes");

    const std::uint8_t *r = poll;
    Frame::MsgPtr msg;
    check(frame.read(msg, r, sizeof(poll)) == comms::ErrorStatus::Success,
          "read poll");
    check(msg && msg->getId() == cc_ublox::MsgId_MonVer, "poll id");

    // MON-RXR with its awake flag set: payload 01, checksum 2D BA.
    const std::uint8_t rxr[] = {0xB5, 0x62, 0x0A, 0x21, 0x01, 0x00, 0x01, 0x2D, 0xBA};
    r = rxr;
    msg.reset();
    check(frame.read(msg, r, sizeof(rxr)) == comms::ErrorStatus::Success,
          "read rxr");
    check(msg && msg->getId() == cc_ublox::MsgId_MonRxr, "rxr id");
    if (msg && msg->getId() == cc_ublox::MsgId_MonRxr) {
        check(static_cast<MonRxr &>(*msg).field_flags().getBitValue_awake(),
              "rxr awake flag");
    }

    MonRxr rxrMsg;
    rxrMsg.field_flags().setBitValue_awake(true);
    w = buf;
    check(frame.write(rxrMsg, w, sizeof(buf)) == comms::ErrorStatus::Success,
          "write rxr");
    check(static_cast<std::size_t>(w - buf) == sizeof(rxr), "rxr length");
    check(same(buf, rxr, sizeof(rxr)), "rxr bytes");

    // A flipped checksum byte and a wrong sync byte are both rejected.
    std::uint8_t bad[sizeof(rxr)];
    for (std::size_t i = 0; i < sizeof(rxr); ++i) {
        bad[i] = rxr[i];
    }
    bad[8] ^= 0xFF;
    r = bad;
    msg.reset();
    check(frame.read(msg, r, sizeof(bad)) != comms::ErrorStatus::Success && !msg,
          "bad checksum rejected");
    bad[8] = rxr[8];
    bad[0] = 0x00;
    r = bad;
    check(frame.read(msg, r, sizeof(bad)) != comms::ErrorStatus::Success && !msg,
          "bad sync rejected");

    // A truncated frame asks for more data.
    r = rxr;
    check(frame.read(msg, r, sizeof(rxr) - 1) ==
              comms::ErrorStatus::NotEnoughData,
          "truncated frame");

    return failures == 0 ? 0 : 1;
}
