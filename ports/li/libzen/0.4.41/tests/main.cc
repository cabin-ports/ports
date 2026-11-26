#include <cstdio>
#include <string>

#include "ZenLib/BitStream.h"
#include "ZenLib/Ztring.h"
#include "ZenLib/ZtringList.h"
#include "ZenLib/int128u.h"
#include "ZenLib/Utils.h"

using namespace ZenLib;

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);              \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  // Ztring: UTF-8 round trip, number formatting and parsing, editing.
  Ztring accent;
  accent.From_UTF8("\xC3\xA9t\xC3\xA9");
  CHECK(accent.size() == 3);
  CHECK(accent.To_UTF8() == "\xC3\xA9t\xC3\xA9");
  CHECK(Ztring::ToZtring(static_cast<int32u>(255), 16) == __T("FF"));
  CHECK(Ztring::ToZtring(static_cast<int32u>(5), 2) == __T("101"));
  CHECK(Ztring::ToZtring(static_cast<int32s>(-42)) == __T("-42"));
  CHECK(Ztring::ToZtring(static_cast<float64>(3.14159), 2) == __T("3.14"));
  CHECK(Ztring(__T("12345")).To_int32u() == 12345u);
  CHECK(Ztring(__T("-7")).To_int32s() == -7);
  CHECK(Ztring(__T("2.5")).To_float64() == 2.5);
  Ztring padded(__T("  hi  "));
  padded.Trim();
  CHECK(padded == __T("hi"));
  padded.MakeUpperCase();
  CHECK(padded == __T("HI"));
  Ztring banana(__T("banana"));
  banana.FindAndReplace(__T("a"), __T("xy"), 0, Ztring_Recursive);
  CHECK(banana == __T("bxynxynxy"));
  CHECK(Ztring().Date_From_Seconds_1970(static_cast<int64s>(86400)) ==
        __T("UTC 1970-01-02 00:00:00"));
  CHECK(Ztring().Date_From_Seconds_1904(static_cast<int64s>(2082844800LL + 3661)) ==
        __T("UTC 1970-01-01 01:01:01"));

  // ZtringList: split on ';' with quoting, and write back.
  ZtringList list(__T("a;b;\"c;d\""));
  CHECK(list.size() == 3);
  CHECK(list[0] == __T("a"));
  CHECK(list[2] == __T("c;d"));
  CHECK(list.Find(__T("b")) == 1);
  CHECK(list.Read() == __T("a;b;\"c;d\""));
  ZtringList comma;
  comma.Separator_Set(0, __T(","));
  comma.Write(__T("x,y,z"));
  CHECK(comma.size() == 3 && comma[1] == __T("y"));

  // int128u: carries across the 64-bit halves, shifts, division.
  int128u max64(static_cast<int64u>(0xFFFFFFFFFFFFFFFFULL));
  CHECK(std::string((max64 + 1).toString()) == "18446744073709551616");
  CHECK(std::string(((max64 + 1) * 3).toString()) ==
        "55340232221128654848");
  int128u one(1);
  int128u big = one << 100;
  CHECK(std::string(big.toString()) == "1267650600228229401496703205376");
  CHECK(std::string(big.toString(16)) == "10000000000000000000000000");
  CHECK((big >> 100) == one);
  CHECK(std::string((big / int128u(1024)).toString()) == "1237940039285380274899124224");
  int128u sextillion(1000000000);
  sextillion = sextillion * sextillion * int128u(1000);
  CHECK(std::string((sextillion / int128u(97)).toString()) ==
        "10309278350515463917");
  CHECK(std::string((sextillion % int128u(97)).toString()) == "51");

  // BitStream: MSB-first reads across byte boundaries, then underrun.
  const int8u bytes[] = {0xA5, 0x3C, 0xFF};
  BitStream bits(bytes, sizeof(bytes));
  CHECK(bits.Get(4) == 0xA);
  CHECK(bits.Get(4) == 0x5);
  CHECK(bits.Get(3) == 1);
  CHECK(bits.Get(5) == 28);
  CHECK(bits.Get1(8) == 0xFF);
  CHECK(bits.Remain() == 0);

  // Utils: byte-order helpers.
  const char be[] = {'\x01', '\x02', '\x03', '\x04'};
  CHECK(BigEndian2int32u(be) == 0x01020304u);
  CHECK(LittleEndian2int32u(be) == 0x04030201u);
  CHECK(LittleEndian2int24u(be) == 0x030201u);
  const char neg[] = {'\xFF', '\xFE'};
  CHECK(BigEndian2int16s(neg) == -2);
  char out[4];
  int32u2BigEndian(out, 0xDEADBEEFu);
  CHECK(static_cast<int8u>(out[0]) == 0xDE && static_cast<int8u>(out[3]) == 0xEF);
  CHECK(BigEndian2int32u(out) == 0xDEADBEEFu);

  if (failures == 0) std::printf("libzen ok\n");
  return failures == 0 ? 0 : 1;
}
