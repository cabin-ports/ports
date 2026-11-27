#include <cloudini_lib/cloudini.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

using namespace Cloudini;

namespace {

constexpr uint32_t kPoints = 1000;
constexpr uint32_t kStep = 13;  // x, y, z as FLOAT32, then intensity as UINT8
constexpr float kResolution = 0.001f;

int failures = 0;

void check(bool ok, const char* what, const char* label) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s (%s)\n", what, label);
    ++failures;
  }
}

std::vector<uint8_t> makeCloud() {
  std::vector<uint8_t> cloud(kPoints * kStep);
  for (uint32_t i = 0; i < kPoints; ++i) {
    const float xyz[3] = {0.01f * i, -0.02f * i, 1.5f + 0.0037f * (i % 7)};
    uint8_t* point = cloud.data() + i * kStep;
    std::memcpy(point, xyz, sizeof(xyz));
    point[12] = static_cast<uint8_t>(i * 7);
  }
  return cloud;
}

float floatAt(const std::vector<uint8_t>& cloud, uint32_t point, uint32_t offset) {
  float value;
  std::memcpy(&value, cloud.data() + point * kStep + offset, sizeof(value));
  return value;
}

EncodingInfo makeInfo(EncodingOptions encoding, CompressionOption compression) {
  const std::optional<float> resolution =
      encoding == EncodingOptions::LOSSY ? std::optional<float>(kResolution) : std::nullopt;
  EncodingInfo info;
  info.width = kPoints;
  info.height = 1;
  info.point_step = kStep;
  info.encoding_opt = encoding;
  info.compression_opt = compression;
  info.fields = {
      {"x", 0, FieldType::FLOAT32, resolution},
      {"y", 4, FieldType::FLOAT32, resolution},
      {"z", 8, FieldType::FLOAT32, resolution},
      {"intensity", 12, FieldType::UINT8, std::nullopt},
  };
  return info;
}

// Encodes with the default worker thread, then decodes from the header the payload carries.
std::vector<uint8_t> roundTrip(const EncodingInfo& info, const std::vector<uint8_t>& cloud, const char* label) {
  PointcloudEncoder encoder(info);
  std::vector<uint8_t> encoded;
  encoder.encode(ConstBufferView(cloud.data(), cloud.size()), encoded);

  ConstBufferView payload(encoded.data(), encoded.size());
  const EncodingInfo header = DecodeHeader(payload);
  check(header == info, "decoded header", label);

  std::vector<uint8_t> decoded;
  PointcloudDecoder().decode(header, payload, decoded);
  return decoded;
}

}  // namespace

int main() {
  const std::vector<uint8_t> cloud = makeCloud();

  for (const auto compression : {CompressionOption::LZ4, CompressionOption::ZSTD}) {
    const char* label = ToString(compression);

    const auto lossless = roundTrip(makeInfo(EncodingOptions::LOSSLESS, compression), cloud, label);
    check(lossless == cloud, "lossless bytes", label);

    const auto lossy = roundTrip(makeInfo(EncodingOptions::LOSSY, compression), cloud, label);
    check(lossy.size() == cloud.size(), "lossy size", label);
    if (lossy.size() != cloud.size()) {
      continue;
    }
    bool within = true;
    bool intensity = true;
    for (uint32_t i = 0; i < kPoints; ++i) {
      for (uint32_t offset : {0u, 4u, 8u}) {
        // Half the resolution (basic_types.hpp), with the float slack of upstream's test_v6.cpp.
        const float in = floatAt(cloud, i, offset);
        within &= std::fabs(floatAt(lossy, i, offset) - in) <= 0.5f * kResolution * 1.001f + 1e-6f * std::fabs(in);
      }
      intensity &= lossy[i * kStep + 12] == cloud[i * kStep + 12];
    }
    check(within, "lossy x, y, z within half the resolution", label);
    check(intensity, "lossy intensity bytes", label);
  }

  if (failures == 0) {
    std::puts("cloudini: ok");
  }
  return failures == 0 ? 0 : 1;
}
