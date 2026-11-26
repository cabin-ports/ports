#include <cstdio>
#include <string>

#include <taglib/fileref.h>
#include <taglib/id3v2tag.h>
#include <taglib/tbytevector.h>
#include <taglib/tbytevectorstream.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tversionnumber.h>
#include <taglib/wavfile.h>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void le16(std::string &out, unsigned int v) {
  out += static_cast<char>(v & 0xff);
  out += static_cast<char>((v >> 8) & 0xff);
}

void le32(std::string &out, unsigned long v) {
  for (int i = 0; i < 4; ++i)
    out += static_cast<char>((v >> (8 * i)) & 0xff);
}

// 100 ms of 8 kHz 16-bit mono PCM silence, followed by an "ID3 " chunk
// holding an ID3v2.4 tag whose one TIT2 frame is zlib-compressed: frame
// flags 0x09 (compression and data length indicator), a data length of 11,
// then the deflate stream of "\x03Compressed" (UTF-8 "Compressed").
std::string makeWav() {
  static const unsigned char id3[] = {
    'I', 'D', '3', 4, 0, 0, 0, 0, 0, 33,
    'T', 'I', 'T', '2', 0, 0, 0, 23, 0x00, 0x09, 0, 0, 0, 11,
    0x78, 0xda, 0x63, 0x76, 0xce, 0xcf, 0x2d, 0x28, 0x4a, 0x2d,
    0x2e, 0x4e, 0x4d, 0x01, 0x00, 0x16, 0x21, 0x04, 0x19,
  };
  std::string s = "RIFF";
  le32(s, 1688);
  s += "WAVE";
  s += "fmt ";
  le32(s, 16);
  le16(s, 1);      // PCM
  le16(s, 1);      // channels
  le32(s, 8000);   // sample rate
  le32(s, 16000);  // byte rate
  le16(s, 2);      // block align
  le16(s, 16);     // bits per sample
  s += "data";
  le32(s, 1600);
  s.append(1600, '\0');
  s += "ID3 ";
  le32(s, sizeof id3);
  s.append(reinterpret_cast<const char *>(id3), sizeof id3);
  s += '\0';  // pad byte of the odd-sized chunk
  return s;
}

}  // namespace

int main() {
  const TagLib::String uber("\xc3\x9c" "ber", TagLib::String::UTF8);
  check(uber.size() == 4, "UTF-8 decodes to four characters");
  check(uber.to8Bit(true) == "\xc3\x9c" "ber", "UTF-8 round trip");
  check(TagLib::ByteVector::fromUInt(0x12345678U).toHex() == "12345678",
        "fromUInt big-endian hex");
  check(TagLib::ByteVector("abc").toBase64() == "YWJj", "base64");
  check(TagLib::runtimeVersion() == TagLib::VersionNumber(2, 3, 2),
        "runtime version 2.3.2");

  const std::string bytes = makeWav();
  check(bytes.size() == 1696, "fixture size");
  TagLib::ByteVectorStream stream(TagLib::ByteVector(
      bytes.data(), static_cast<unsigned int>(bytes.size())));

  {
    TagLib::RIFF::WAV::File wav(&stream);
    check(wav.isValid(), "WAV is valid");
    const TagLib::RIFF::WAV::Properties *props = wav.audioProperties();
    check(props != nullptr, "WAV has audio properties");
    if (props != nullptr) {
      check(props->sampleRate() == 8000, "sample rate 8000");
      check(props->channels() == 1, "one channel");
      check(props->bitsPerSample() == 16, "16 bits per sample");
      check(props->lengthInMilliseconds() == 100, "100 ms long");
      check(props->bitrate() == 128, "128 kb/s");
    }
    check(wav.hasID3v2Tag(), "ID3v2 chunk found");
    check(wav.ID3v2Tag()->title() == "Compressed",
          "zlib-compressed TIT2 frame decoded");

    wav.tag()->setTitle(uber);
    wav.tag()->setArtist("Ports");
    check(wav.save(), "WAV saved");
  }

  // Content detection, which needs the formats taglib_config.h enables.
  stream.seek(0);
  TagLib::FileRef ref(&stream);
  check(!ref.isNull(), "FileRef detects the saved stream");
  if (!ref.isNull()) {
    check(dynamic_cast<TagLib::RIFF::WAV::File *>(ref.file()) != nullptr,
          "detected as WAV");
    check(ref.tag()->title() == uber, "saved title read back");
    check(ref.tag()->artist() == "Ports", "saved artist read back");
    const TagLib::PropertyMap props = ref.properties();
    check(props.contains("TITLE") &&
              props["TITLE"].front().to8Bit(true) == "\xc3\x9c" "ber",
          "TITLE property");
  }

  if (failures == 0)
    std::puts("taglib: all checks passed");
  return failures == 0 ? 0 : 1;
}
