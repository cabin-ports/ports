// src/tick is the output of `sbeppc --output-dir src schema/tick.xml`
// (sbeppc 1.8.0, built from the same archive).
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include <tick/tick.hpp>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  std::array<char, 256> buf;
  buf.fill(0x7f);

  // Encode, setting fields out of order.
  auto m = sbepp::make_view<tick::messages::Order>(buf.data(), buf.size());
  sbepp::fill_message_header(m);
  m.price(-12345);
  m.id(0x0102030405060708ULL);
  m.side(tick::types::Side::Sell);
  m.flags(tick::types::Flags{}.Ioc(true).Hidden(true));
  m.symbol().assign_string("ABCD");
  m.qty(sbepp::nullopt);
  auto g = m.fills();
  sbepp::fill_group_header(g, 0);
  g.resize(2);
  g[0].px(100);
  g[0].n(7);
  g[1].px(-200);
  g[1].n(65535);
  m.note().assign_string("hello");

  // 8 header + 26 block + 4 group header + 2 * 10 entries + 4 + 5 data.
  const std::size_t size = sbepp::size_bytes(m);
  check(size == 67, "encoded size");

  // Wire format, little endian.
  const unsigned char* b = reinterpret_cast<const unsigned char*>(buf.data());
  check(b[0] == 26 && b[1] == 0 && b[2] == 1 && b[3] == 0 && b[4] == 7 &&
            b[5] == 0 && b[6] == 1 && b[7] == 0,
        "message header: blockLength 26, template 1, schema 7, version 1");
  check(b[8] == 8 && b[15] == 1, "id is little endian");
  check(std::memcmp(b + 16, "ABCD", 4) == 0, "symbol bytes");
  check(b[20] == 0xc7 && b[21] == 0xcf && b[27] == 0xff, "price -12345");
  check(b[28] == 2 && b[29] == 0x09, "side Sell, flags Ioc|Hidden");
  check(b[30] == 0xff && b[33] == 0xff, "qty is its null value -1");
  check(b[34] == 10 && b[35] == 0 && b[36] == 2 && b[37] == 0,
        "group header: blockLength 10, two entries");
  check(b[38] == 100 && b[46] == 7 && b[48] == 0x38 && b[49] == 0xff &&
            b[56] == 0xff && b[57] == 0xff,
        "group entries");
  check(b[58] == 5 && b[59] == 0 && std::memcmp(b + 62, "hello", 5) == 0,
        "data length prefix and bytes");

  // Decode from a copy of exactly the encoded bytes.
  std::string wire(buf.data(), size);
  auto header = sbepp::make_const_view<
      sbepp::schema_traits<tick::schema>::header_type>(wire.data(),
                                                       wire.size());
  check(*header.templateId() ==
            sbepp::message_traits<tick::schema::messages::Order>::id(),
        "template id matches the message");
  auto d = sbepp::make_const_view<tick::messages::Order>(wire.data(), wire.size());
  check(sbepp::size_bytes_checked(d, wire.size()).valid &&
            sbepp::size_bytes_checked(d, wire.size()).size == 67,
        "size_bytes_checked accepts the whole message");
  check(!sbepp::size_bytes_checked(d, wire.size() - 1).valid,
        "size_bytes_checked rejects a truncated buffer");
  check(*d.id() == 0x0102030405060708ULL, "decoded id");
  check(*d.price() == -12345, "decoded price");
  check(d.side() == tick::types::Side::Sell, "decoded side");
  check(d.flags().Ioc() && d.flags().Hidden(), "decoded flags");
  check(std::string(d.symbol().data(), 4) == "ABCD", "decoded symbol");
  check(!d.qty().has_value(), "decoded qty is null");
  auto dg = d.fills();
  check(dg.size() == 2 && dg[0].px().value() == 100 && dg[0].n().value() == 7 &&
            dg[1].px().value() == -200 && dg[1].n().value() == 65535,
        "decoded group");
  auto note = d.note();
  check(std::string(note.data(), note.size()) == "hello", "decoded data");

  // Setting an optional field to a value leaves the message size unchanged.
  m.qty(42);
  check(*sbepp::make_const_view<tick::messages::Order>(buf.data(), buf.size()).qty() == 42,
        "optional qty set to 42");
  check(sbepp::size_bytes(m) == 67, "size is unchanged by setting qty");

  if (failures != 0) {
    return 1;
  }
  std::printf("sbepp: Order round-trips in %zu bytes\n", size);
  return 0;
}
