// Writes a chunked MCAP file into memory with each compression and reads it back.
#define MCAP_IMPLEMENTATION
#include <mcap/mcap.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

class StringReader final : public mcap::IReadable {
public:
  explicit StringReader(std::string data) : data_(std::move(data)) {}
  uint64_t size() const override { return data_.size(); }
  uint64_t read(std::byte** output, uint64_t offset, uint64_t size) override {
    if (offset >= data_.size()) return 0;
    size = std::min<uint64_t>(size, data_.size() - offset);
    *output = reinterpret_cast<std::byte*>(data_.data()) + offset;
    return size;
  }

private:
  std::string data_;
};

void roundTrip(mcap::Compression compression, const char* name) {
  const char* payloads[] = {"a", "bb", "ccc"};
  // Compressible bulk so the chunk really goes through the codec.
  const std::string bulk(4096, 'x');

  std::ostringstream os;
  mcap::McapWriter writer;
  mcap::McapWriterOptions options("ros1");
  options.compression = compression;
  writer.open(os, options);
  mcap::Schema schema("std_msgs/String", "ros1msg", "string data");
  writer.addSchema(schema);
  mcap::Channel channel("/chatter", "ros1", schema.id);
  writer.addChannel(channel);
  for (uint32_t i = 0; i < 4; ++i) {
    mcap::Message msg;
    msg.channelId = channel.id;
    msg.sequence = i + 1;
    msg.logTime = msg.publishTime = (i + 1) * 10;
    const std::string payload = i < 3 ? payloads[i] : bulk;
    msg.data = reinterpret_cast<const std::byte*>(payload.data());
    msg.dataSize = payload.size();
    check(writer.write(msg).ok(), "write message");
  }
  writer.close();
  const std::string file = os.str();

  // Magic at both ends.
  check(file.size() > 16 && std::memcmp(file.data(), mcap::Magic, 8) == 0 &&
          std::memcmp(file.data() + file.size() - 8, mcap::Magic, 8) == 0,
        "magic bytes");
  // The compressed file is smaller than the bulk it holds.
  check(file.size() < bulk.size(), "chunk is compressed");

  StringReader readable(file);
  mcap::McapReader reader;
  check(reader.open(readable).ok(), "open");
  check(reader.readSummary(mcap::ReadSummaryMethod::NoFallbackScan).ok(), "summary");
  const auto stats = reader.statistics();
  check(stats && stats->messageCount == 4 && stats->channelCount == 1 &&
          stats->schemaCount == 1 && stats->messageStartTime == 10 &&
          stats->messageEndTime == 40,
        "statistics");
  check(reader.chunkIndexes().size() == 1 && reader.chunkIndexes()[0].compression == name,
        "chunk compression name");

  std::vector<std::string> got;
  uint64_t expectTime = 10;
  for (const auto& view : reader.readMessages()) {
    check(view.channel->topic == "/chatter", "topic");
    check(view.schema->name == "std_msgs/String", "schema name");
    check(view.message.logTime == expectTime && view.message.publishTime == expectTime,
          "timestamps");
    check(view.message.sequence == expectTime / 10, "sequence");
    expectTime += 10;
    got.emplace_back(reinterpret_cast<const char*>(view.message.data), view.message.dataSize);
  }
  check(got.size() == 4 && got[0] == "a" && got[1] == "bb" && got[2] == "ccc" && got[3] == bulk,
        "payloads");
  reader.close();
}

}  // namespace

int main() {
  check(std::string(mcap::LibraryVersion) == "2.2.0", "library version");
  roundTrip(mcap::Compression::Zstd, "zstd");
  roundTrip(mcap::Compression::Lz4, "lz4");
  if (failures == 0) std::puts("OK");
  return failures == 0 ? 0 : 1;
}
