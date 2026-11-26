// Defines a small protocol message from comms fields (integer, length-prefixed
// string, a bundle with an enum), serialises it big-endian, checks the exact
// bytes, reads it back, and checks that truncated and invalid input are
// reported.
#include <comms/comms.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

namespace {

using Interface = comms::Message<
    comms::option::def::BigEndian,
    comms::option::def::MsgIdType<std::uint8_t>,
    comms::option::app::ReadIterator<const std::uint8_t *>,
    comms::option::app::WriteIterator<std::uint8_t *>,
    comms::option::app::LengthInfoInterface,
    comms::option::app::ValidCheckInterface>;

using Base = Interface::Field;

enum class Color : std::uint8_t { Red, Green, Blue };

using Id = comms::field::IntValue<Base, std::uint16_t>;
using Name = comms::field::String<
    Base,
    comms::option::def::SequenceSizeFieldPrefix<
        comms::field::IntValue<Base, std::uint8_t>>>;
using Attr = comms::field::Bundle<
    Base,
    std::tuple<
        comms::field::IntValue<Base, std::uint8_t>,
        comms::field::EnumValue<
            Base, Color,
            comms::option::def::FixedLength<1>,
            comms::option::def::ValidNumValueRange<0, 2>>>>;

class Msg : public comms::MessageBase<
                Interface,
                comms::option::def::StaticNumIdImpl<7>,
                comms::option::def::FieldsImpl<std::tuple<Id, Name, Attr>>,
                comms::option::def::MsgType<Msg>>
{
};

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    Msg msg;
    auto &f = msg.fields();
    std::get<0>(f).value() = 0x1234;
    std::get<1>(f).value() = "abc";
    std::get<2>(f).value() = std::make_tuple(
        comms::field::IntValue<Base, std::uint8_t>(5),
        comms::field::EnumValue<
            Base, Color, comms::option::def::FixedLength<1>,
            comms::option::def::ValidNumValueRange<0, 2>>(Color::Green));

    check(Msg::doGetId() == 7, "static message id");
    check(msg.valid(), "message valid");

    const std::vector<std::uint8_t> expected = {
        0x12, 0x34, 0x03, 'a', 'b', 'c', 0x05, 0x01};
    check(msg.length() == expected.size(), "length");

    std::vector<std::uint8_t> buf(msg.length());
    std::uint8_t *w = buf.data();
    check(msg.write(w, buf.size()) == comms::ErrorStatus::Success, "write");
    check(buf == expected, "serialised bytes");

    Msg in;
    const std::uint8_t *r = buf.data();
    check(in.read(r, buf.size()) == comms::ErrorStatus::Success, "read");
    check(r == buf.data() + buf.size(), "read consumed all bytes");
    auto &g = in.fields();
    check(std::get<0>(g).value() == 0x1234, "id round trip");
    check(std::get<1>(g).value() == "abc", "name round trip");
    check(std::get<0>(std::get<2>(g).value()).value() == 5, "count round trip");
    check(std::get<1>(std::get<2>(g).value()).value() == Color::Green,
          "enum round trip");

    Msg cut;
    r = buf.data();
    check(cut.read(r, buf.size() - 1) == comms::ErrorStatus::NotEnoughData,
          "truncated buffer");

    // The length prefix promises more characters than the buffer holds.
    std::vector<std::uint8_t> shortName = {0x12, 0x34, 0x09, 'a'};
    r = shortName.data();
    check(cut.read(r, shortName.size()) == comms::ErrorStatus::NotEnoughData,
          "truncated string");

    // Color has three values; 3 is rejected by ValidNumValueRange.
    std::vector<std::uint8_t> bad = expected;
    bad.back() = 3;
    Msg invalid;
    r = bad.data();
    invalid.read(r, bad.size());
    check(!invalid.valid(), "out-of-range enum is invalid");

    return failures == 0 ? 0 : 1;
}
