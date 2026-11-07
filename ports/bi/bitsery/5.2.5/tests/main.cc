#include <bitsery/adapter/buffer.h>
#include <bitsery/adapter/measure_size.h>
#include <bitsery/bitsery.h>
#include <bitsery/ext/compact_value.h>
#include <bitsery/ext/std_map.h>
#include <bitsery/ext/value_range.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

enum class Kind : uint16_t { Sensor = 1, Actuator = 7 };

struct Reading {
  int32_t tenths; // temperature in 0.1 degrees, -40.0 .. 125.0
  bool valid;
};

struct Device {
  uint32_t id;
  Kind kind;
  std::string name;
  uint64_t uptime;
  std::map<std::string, uint16_t> ports;
  std::vector<Reading> readings;
};

template <typename S> void serialize(S &s, Device &d) {
  s.value4b(d.id);
  s.value2b(d.kind);
  s.text1b(d.name, 32);
  s.ext8b(d.uptime, bitsery::ext::CompactValue{});
  s.ext(d.ports, bitsery::ext::StdMap{8},
        [](S &m, std::string &key, uint16_t &port) {
          m.text1b(key, 16);
          m.value2b(port);
        });
  // -400 .. 1250 is 1651 values, so 11 bits, plus 1 bit for valid.
  s.enableBitPacking([&d](typename S::BPEnabledType &b) {
    b.container(d.readings, 100,
                [](typename S::BPEnabledType &r, Reading &reading) {
                  r.ext(reading.tenths,
                        bitsery::ext::ValueRange<int32_t>{-400, 1250});
                  r.boolValue(reading.valid);
                });
  });
}

// The same text with a larger limit, to write what Device must reject.
struct LongName {
  std::string name;
};
template <typename S> void serialize(S &s, LongName &n) { s.text1b(n.name, 255); }
struct ShortName {
  std::string name;
};
template <typename S> void serialize(S &s, ShortName &n) { s.text1b(n.name, 32); }

using Buffer = std::vector<uint8_t>;
using Output = bitsery::OutputBufferAdapter<Buffer>;
using Input = bitsery::InputBufferAdapter<Buffer>;

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

static bool same(const Device &a, const Device &b) {
  if (a.id != b.id || a.kind != b.kind || a.name != b.name ||
      a.uptime != b.uptime || a.ports != b.ports ||
      a.readings.size() != b.readings.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.readings.size(); ++i) {
    if (a.readings[i].tenths != b.readings[i].tenths ||
        a.readings[i].valid != b.readings[i].valid) {
      return false;
    }
  }
  return true;
}

static std::size_t size_of(Device d) {
  Buffer buffer;
  return bitsery::quickSerialization<Output>(buffer, d);
}

int main() {
  Device device{0x01020304u, Kind::Actuator, "pump-3", 300, {}, {}};
  device.ports["http"] = 8080;
  device.ports["mqtt"] = 1883;
  device.readings.push_back(Reading{-400, true});
  device.readings.push_back(Reading{215, true});
  device.readings.push_back(Reading{1250, false});

  Buffer buffer;
  const std::size_t written = bitsery::quickSerialization<Output>(buffer, device);
  Device copy{};
  const std::pair<bitsery::ReaderError, bool> state =
      bitsery::quickDeserialization<Input>({buffer.begin(), written}, copy);
  check(state.first == bitsery::ReaderError::NoError && state.second,
        "round trip reads the whole buffer without error");
  check(same(device, copy), "round trip restores every field");

  // The default configuration is little-endian: value4b(0x01020304).
  check(written > 6 && buffer[0] == 0x04 && buffer[1] == 0x03 &&
            buffer[2] == 0x02 && buffer[3] == 0x01,
        "value4b writes little-endian bytes");
  check(buffer[4] == 7 && buffer[5] == 0, "value2b writes the enum value");
  // text1b: one size byte, then the characters.
  check(buffer[6] == 6 && std::string(buffer.begin() + 7, buffer.begin() + 13) == "pump-3",
        "text1b writes a size prefix and the bytes");

  // MeasureSize computes the same size without writing.
  bitsery::Serializer<bitsery::MeasureSize> measure;
  measure.object(device);
  check(measure.adapter().writtenBytesCount() == written,
        "MeasureSize agrees with the written size");

  // CompactValue stores 7 bits per byte: 127 takes 1 byte, 300 takes 2,
  // 2^40 takes 6.
  Device small = device;
  small.uptime = 127;
  check(size_of(small) == written - 1, "CompactValue stores 127 in one byte");
  Device big = device;
  big.uptime = uint64_t{1} << 40;
  check(size_of(big) == written + 4, "CompactValue grows with the value");
  Buffer big_buffer;
  const std::size_t big_size = bitsery::quickSerialization<Output>(big_buffer, big);
  Device big_copy{};
  check(bitsery::quickDeserialization<Input>({big_buffer.begin(), big_size}, big_copy).second &&
            same(big, big_copy),
        "CompactValue round-trips 2^40");

  // Bit packing: 8 more readings add 8 * 12 bits, exactly 12 bytes.
  Device more = device;
  for (int i = 0; i < 8; ++i) {
    more.readings.push_back(Reading{i * 10, i % 2 == 0});
  }
  check(size_of(more) == written + 12, "ValueRange packs each reading in 12 bits");

  // A truncated buffer reports DataOverflow.
  Device partial{};
  const std::pair<bitsery::ReaderError, bool> truncated =
      bitsery::quickDeserialization<Input>({buffer.begin(), written - 3}, partial);
  check(truncated.first == bitsery::ReaderError::DataOverflow && !truncated.second,
        "a truncated buffer is DataOverflow");

  // A text longer than the reader's limit is InvalidData.
  LongName long_name{std::string(200, 'x')};
  Buffer name_buffer;
  const std::size_t name_size = bitsery::quickSerialization<Output>(name_buffer, long_name);
  // Sizes of 128 and more take a two-byte prefix.
  check(name_buffer[0] == 0x80 && name_buffer[1] == 200, "a size of 200 takes the two-byte prefix");
  LongName long_copy{};
  check(bitsery::quickDeserialization<Input>({name_buffer.begin(), name_size}, long_copy).second &&
            long_copy.name == long_name.name,
        "a 200-byte text round-trips");
  ShortName short_name{};
  const std::pair<bitsery::ReaderError, bool> too_long =
      bitsery::quickDeserialization<Input>({name_buffer.begin(), name_size}, short_name);
  check(too_long.first == bitsery::ReaderError::InvalidData,
        "text over the size limit is InvalidData");

  std::printf("bitsery (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
