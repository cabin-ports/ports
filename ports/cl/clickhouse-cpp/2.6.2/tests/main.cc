#include <clickhouse/base/buffer.h>
#include <clickhouse/base/compressed.h>
#include <clickhouse/base/input.h>
#include <clickhouse/base/output.h>
#include <clickhouse/block.h>
#include <clickhouse/columns/decimal.h>
#include <clickhouse/columns/factory.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/exceptions.h>

#include <cstdio>
#include <cstring>
#include <string>

#if !CH_MAP_BOOL_TO_UINT8
#error "CH_MAP_BOOL_TO_UINT8 must reach consumers as 1, as the library is built"
#endif

using namespace clickhouse;

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void type_parsing() {
  auto col = CreateColumnByType("SimpleAggregateFunction(funt, Int32)");
  check(col && col->Type()->GetName() == "Int32", "SimpleAggregateFunction name");
  check(col && col->Type()->GetCode() == Type::Int32, "SimpleAggregateFunction code");
  check(col && col->As<ColumnInt32>() != nullptr, "SimpleAggregateFunction column");

  auto boolean = CreateColumnByType("Bool");
  check(boolean && boolean->Type()->GetCode() == Type::UInt8, "Bool maps to UInt8");
  check(boolean && boolean->As<ColumnUInt8>() != nullptr, "Bool column");

  auto dec = CreateColumnByType("Decimal(38, 2)");
  check(dec && dec->Type()->GetName() == "Decimal(38,2)", "Decimal name");

  check(CreateColumnByType("FixedString(10") == nullptr, "unmatched bracket rejected");
}

static void columns_and_block() {
  auto ids = std::make_shared<ColumnUInt64>();
  ids->Append(1);
  ids->Append(2);
  ids->Append(3);
  check(ids->Size() == 3 && ids->At(1) == 2, "ColumnUInt64 append");

  auto prices = CreateColumnByType("Decimal(38, 2)")->As<ColumnDecimal>();
  prices->Append("-12345678901234567890.12");
  prices->Append("7.5");
  prices->Append("0.01");
  const Int128 big = -(Int128(1234567890123456789) * 1000 + 12);
  check(prices->At(0) == big, "Decimal 128-bit parse");
  check(prices->At(1) == 750 && prices->At(2) == 1, "Decimal scale");
  bool overflow = false;
  try {
    prices->Append("1" + std::string(39, '0'));
  } catch (const AssertionError&) {
    overflow = true;
  }
  check(overflow && prices->Size() == 3, "Decimal overflow rejected");

  Block block;
  block.AppendColumn("id", ids);
  block.AppendColumn("price", prices);
  check(block.GetColumnCount() == 2 && block.GetRowCount() == 3, "Block shape");
}

// CompressedOutput frames an LZ4 block as CityHash128 (CityHash 1.0.2, which
// ClickHouse servers check) of the 9-byte header and payload, then the header
// (method 0x82, compressed and original sizes) and the payload.  LZ4 stores an
// input this short as one literal run.  The hash is stock Google CityHash
// v1.0.2's (google/cityhash 10506c56, which passes its own city-test.cc).
static void lz4_round_trip() {
  static const unsigned char expected[38] = {
      0x82, 0x9f, 0xc4, 0xb6, 0x2c, 0xfb, 0x04, 0xfa, 0x89, 0xdb, 0x57, 0xfb,
      0x8d, 0xc6, 0x4f, 0xd6, 0x82, 0x16, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00,
      0x00, 0xc0, 'h',  'e',  'l',  'l',  'o',  ',',  ' ',  'w',  'o',  'r',
      'l',  'd'};
  const char text[] = "hello, world";

  Buffer wire;
  {
    BufferOutput sink(&wire);
    CompressedOutput out(&sink, 0, CompressionMethod::LZ4);
    out.Write(text, 12);
    out.Flush();
  }
  check(wire.size() == sizeof expected &&
            std::memcmp(wire.data(), expected, sizeof expected) == 0,
        "LZ4 block bytes");

  {
    ArrayInput source(wire.data(), wire.size());
    CompressedInput in(&source);
    char back[12] = {};
    check(in.Read(back, sizeof back) == sizeof back &&
              std::memcmp(back, text, sizeof back) == 0,
          "LZ4 block decoded");
    char more;
    check(in.Read(&more, 1) == 0, "end of stream");
  }

  wire[30] ^= 1;
  bool rejected = false;
  try {
    ArrayInput source(wire.data(), wire.size());
    CompressedInput in(&source);
    char back[12];
    in.Read(back, sizeof back);
  } catch (const CompressionError&) {
    rejected = true;
  }
  check(rejected, "corrupted block rejected");
}

int main() {
  type_parsing();
  columns_and_block();
  lz4_round_trip();
  if (failures != 0) {
    return 1;
  }
  std::puts("clickhouse-cpp: ok");
  return 0;
}
