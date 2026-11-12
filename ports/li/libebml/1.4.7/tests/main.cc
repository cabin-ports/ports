// C++ consumer: renders EBML elements into memory, checks the bytes against
// RFC 8794's encoding, and parses them back.
#include <ebml/EbmlContexts.h>
#include <ebml/EbmlCrc32.h>
#include <ebml/EbmlDate.h>
#include <ebml/EbmlFloat.h>
#include <ebml/EbmlHead.h>
#include <ebml/EbmlSInteger.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlSubHead.h>
#include <ebml/EbmlUnicodeString.h>
#include <ebml/EbmlVersion.h>
#include <ebml/MemIOCallback.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace libebml;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("main.cc:%d: check failed: %s\n", __LINE__, #cond);  \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

using Bytes = std::vector<unsigned char>;

Bytes written(const MemIOCallback &io) {
  return Bytes(io.GetDataBuffer(), io.GetDataBuffer() + io.GetDataBufferSize());
}

// Elements a consumer defines with the library's own class macros.
class TestDouble : public EbmlFloat {
 public:
  TestDouble() : EbmlFloat(EbmlFloat::FLOAT_64) {}
  EBML_CONCRETE_CLASS(TestDouble)
};
class TestSingle : public EbmlFloat {
 public:
  TestSingle() : EbmlFloat(EbmlFloat::FLOAT_32) {}
  EBML_CONCRETE_CLASS(TestSingle)
};
class TestSigned : public EbmlSInteger {
 public:
  TestSigned() {}
  EBML_CONCRETE_CLASS(TestSigned)
};
class TestDate : public EbmlDate {
 public:
  TestDate() {}
  EBML_CONCRETE_CLASS(TestDate)
};
class TestTitle : public EbmlUnicodeString {
 public:
  TestTitle() {}
  EBML_CONCRETE_CLASS(TestTitle)
};

DEFINE_EBML_CLASS_ORPHAN(TestDouble, 0x4489, 2, "TestDouble")
DEFINE_EBML_CLASS_ORPHAN(TestSingle, 0x4488, 2, "TestSingle")
DEFINE_EBML_CLASS_ORPHAN(TestSigned, 0xFB, 1, "TestSigned")
DEFINE_EBML_CLASS_ORPHAN(TestDate, 0x4461, 2, "TestDate")
DEFINE_EBML_CLASS_ORPHAN(TestTitle, 0x7BA9, 2, "TestTitle")

// Renders `element` and checks its bytes, then parses them back as a T.
template <typename T>
std::unique_ptr<T> round_trip(T &element, const Bytes &expected) {
  MemIOCallback io;
  element.Render(io);
  CHECK(written(io) == expected);
  io.setFilePointer(0);
  EbmlStream stream(io);
  std::unique_ptr<EbmlElement> read(stream.FindNextID(EBML_INFO(T), ~0ULL));
  if (read == nullptr || !(static_cast<const EbmlId &>(*read) == EBML_ID(T))) {
    CHECK(!"element not found");
    return nullptr;
  }
  read->ReadData(io);
  CHECK(read->GetSize() == expected.size() - read->HeadSize());
  return std::unique_ptr<T>(static_cast<T *>(read.release()));
}

void test_header() {
  EbmlHead head;
  GetChild<EDocType>(head).SetValue("webm");
  GetChild<EDocTypeVersion>(head).SetValue(4);
  GetChild<EDocTypeReadVersion>(head).SetValue(2);

  // With defaults written: every child in context order, 31 payload bytes.
  MemIOCallback io;
  head.Render(io, true);
  const Bytes expected = {
      0x1A, 0x45, 0xDF, 0xA3, 0x9F,                    // EBML, size 31
      0x42, 0x86, 0x81, 0x01,                          // EBMLVersion 1
      0x42, 0xF7, 0x81, 0x01,                          // EBMLReadVersion 1
      0x42, 0xF2, 0x81, 0x04,                          // EBMLMaxIDLength 4
      0x42, 0xF3, 0x81, 0x08,                          // EBMLMaxSizeLength 8
      0x42, 0x82, 0x84, 'w', 'e', 'b', 'm',            // DocType
      0x42, 0x87, 0x81, 0x04,                          // DocTypeVersion 4
      0x42, 0x85, 0x81, 0x02,                          // DocTypeReadVersion 2
  };
  CHECK(written(io) == expected);

  // Without defaults, only the three values that differ from them.
  MemIOCallback lean;
  head.Render(lean, false);
  const Bytes lean_expected = {
      0x1A, 0x45, 0xDF, 0xA3, 0x8F, 0x42, 0x82, 0x84, 'w',  'e',
      'b',  'm',  0x42, 0x87, 0x81, 0x04, 0x42, 0x85, 0x81, 0x02,
  };
  CHECK(written(lean) == lean_expected);

  io.setFilePointer(0);
  EbmlStream stream(io);
  std::unique_ptr<EbmlElement> top(stream.FindNextID(EBML_INFO(EbmlHead), ~0ULL));
  const bool is_head = top && static_cast<const EbmlId &>(*top) == EBML_ID(EbmlHead);
  CHECK(is_head);
  if (!is_head) return;
  int upper = 0;
  EbmlElement *found = nullptr;
  top->Read(stream, EBML_CONTEXT(top.get()), upper, found, true);
  auto &parsed = static_cast<EbmlHead &>(*top);
  CHECK(parsed.ListSize() == 7);
  CHECK(GetChild<EVersion>(parsed).GetValue() == 1);
  CHECK(GetChild<EMaxSizeLength>(parsed).GetValue() == 8);
  CHECK(GetChild<EDocType>(parsed).GetValue() == "webm");
  CHECK(GetChild<EDocTypeVersion>(parsed).GetValue() == 4);
  CHECK(GetChild<EDocTypeReadVersion>(parsed).GetValue() == 2);
  CHECK(parsed.CheckMandatory());
}

void test_vint() {
  // RFC 8794 section 4: the shortest length that leaves the all-ones value
  // free for "unknown".
  CHECK(CodedSizeLength(126, 0) == 1);
  CHECK(CodedSizeLength(127, 0) == 2);
  CHECK(CodedSizeLength(16382, 0) == 2);
  CHECK(CodedSizeLength(16383, 0) == 3);
  CHECK(CodedSizeLength(5, 4) == 4);

  binary buffer[8] = {};
  CHECK(CodedValueLength(127, 2, buffer) == 2);
  CHECK(buffer[0] == 0x40 && buffer[1] == 0x7F);
  CHECK(CodedValueLength(5, 4, buffer) == 4);
  CHECK(buffer[0] == 0x10 && buffer[1] == 0 && buffer[2] == 0 && buffer[3] == 5);

  uint32 length = 2;
  uint64 unknown = 0;
  const binary two[] = {0x40, 0x7F};
  CHECK(ReadCodedSizeValue(two, length, unknown) == 127);
  CHECK(length == 2 && unknown == 0x3FFF);
  const binary all_ones[] = {0xFF};
  length = 1;
  CHECK(ReadCodedSizeValue(all_ones, length, unknown) == 0x7F);
  CHECK(length == 1 && unknown == 0x7F);  // all ones: the unknown-size marker
  const binary none[] = {0x00};
  length = 1;
  ReadCodedSizeValue(none, length, unknown);
  CHECK(length == 0);

  // Signed lacing values are offset by half the range: -1 is 62 in one byte.
  CHECK(CodedSizeLengthSigned(-1, 0) == 1);
  CHECK(CodedSizeLengthSigned(-64, 0) == 2);
  CHECK(CodedValueLengthSigned(-1, 1, buffer) == 1);
  CHECK(buffer[0] == 0xBE);
  length = 1;
  CHECK(ReadCodedSizeSignedValue(buffer, length, unknown) == -1);
  CHECK(CodedValueLengthSigned(-100, 2, buffer) == 2);
  length = 2;
  CHECK(ReadCodedSizeSignedValue(buffer, length, unknown) == -100);
}

void test_crc32() {
  // CRC-32/ISO-HDLC check value; EBML stores it little-endian (RFC 8794 11.3.1).
  const std::string input = "123456789";
  const auto *data = reinterpret_cast<const binary *>(input.data());
  EbmlCrc32 crc;
  crc.FillCRC32(data, static_cast<uint32>(input.size()));
  CHECK(crc.GetCrc32() == 0xCBF43926u);
  CHECK(EbmlCrc32::CheckCRC(0xCBF43926u, data, static_cast<uint32>(input.size())));
  CHECK(!EbmlCrc32::CheckCRC(0xCBF43927u, data, static_cast<uint32>(input.size())));

  MemIOCallback io;
  crc.Render(io);
  CHECK(written(io) == (Bytes{0xBF, 0x84, 0x26, 0x39, 0xF4, 0xCB}));
}

void test_values() {
  TestDouble half;
  half.SetValue(0.5);
  auto d = round_trip(half, {0x44, 0x89, 0x88, 0x3F, 0xE0, 0, 0, 0, 0, 0, 0});
  CHECK(d && d->GetValue() == 0.5);

  TestSingle quarter;
  quarter.SetValue(-0.25);
  auto f = round_trip(quarter, {0x44, 0x88, 0x84, 0xBE, 0x80, 0x00, 0x00});
  CHECK(f && f->GetValue() == -0.25);

  TestSigned minus_one;
  minus_one.SetValue(-1);
  auto s = round_trip(minus_one, {0xFB, 0x81, 0xFF});
  CHECK(s && s->GetValue() == -1);

  TestSigned wide;
  wide.SetValue(-129);
  auto w = round_trip(wide, {0xFB, 0x82, 0xFF, 0x7F});
  CHECK(w && w->GetValue() == -129);

  // Nanoseconds since 2001-01-01T00:00:00 UTC; one second after it.
  TestDate date;
  date.SetEpochDate(978307201);
  auto t = round_trip(date, {0x44, 0x61, 0x88, 0, 0, 0, 0, 0x3B, 0x9A, 0xCA, 0x00});
  CHECK(t && t->GetEpochDate() == 978307201);

  // "h", U+00E9 and U+1F600 as UTF-8.
  const std::string utf8 = "h\xC3\xA9\xF0\x9F\x98\x80";
  TestTitle title;
  title.SetValueUTF8(utf8);
  auto u = round_trip(title, {0x7B, 0xA9, 0x87, 'h', 0xC3, 0xA9, 0xF0, 0x9F, 0x98, 0x80});
  CHECK(u && u->GetValueUTF8() == utf8);
  if (u) {
    const UTFstring wide_value = u->GetValue();
    const wchar_t *c = wide_value.c_str();
    if (sizeof(wchar_t) == 2) {
      CHECK(wide_value.length() == 4);
      CHECK(c[0] == L'h' && c[1] == 0xE9 && c[2] == 0xD83D && c[3] == 0xDE00);
    } else {
      CHECK(wide_value.length() == 3);
      CHECK(c[0] == L'h' && c[1] == 0xE9 && static_cast<unsigned long>(c[2]) == 0x1F600ul);
    }
  }
}

}  // namespace

int main() {
  CHECK(EbmlCodeVersion == "1.4.7");
  CHECK(LIBEBML_VERSION == 0x010407);
  test_header();
  test_vint();
  test_crc32();
  test_values();
  std::printf("libebml (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
