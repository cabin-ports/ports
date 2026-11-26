#include <dmlc/config.h>
#include <dmlc/data.h>
#include <dmlc/filesystem.h>
#include <dmlc/io.h>
#include <dmlc/logging.h>
#include <dmlc/memory_io.h>
#include <dmlc/parameter.h>
#include <dmlc/recordio.h>
#include <dmlc/threadediter.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;

#define EXPECT(cond)                                                \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

struct LayerParam : public dmlc::Parameter<LayerParam> {
  int num_hidden;
  float learning_rate;
  std::string name;
  DMLC_DECLARE_PARAMETER(LayerParam) {
    DMLC_DECLARE_FIELD(num_hidden).set_range(0, 1000);
    DMLC_DECLARE_FIELD(learning_rate).set_default(0.25f);
    DMLC_DECLARE_FIELD(name).set_default("layer");
  }
};

DMLC_REGISTER_PARAMETER(LayerParam);

typedef std::vector<std::pair<std::string, std::string> > Kwargs;

static bool InitThrowsParamError(const Kwargs& kwargs) {
  LayerParam param;
  try {
    param.Init(kwargs);
  } catch (const dmlc::ParamError&) {
    return true;
  }
  return false;
}

static void TestParameter() {
  LayerParam param;
  param.Init(Kwargs{{"num_hidden", "64"}});
  EXPECT(param.num_hidden == 64);
  EXPECT(param.learning_rate == 0.25f);
  EXPECT(param.name == "layer");

  param.Init(Kwargs{{"num_hidden", "1000"}, {"learning_rate", "0.5"},
                    {"name", "fc1"}});
  EXPECT(param.num_hidden == 1000);
  EXPECT(param.learning_rate == 0.5f);
  EXPECT(param.name == "fc1");
  std::map<std::string, std::string> dict = param.__DICT__();
  EXPECT(dict.size() == 3);
  EXPECT(dict["num_hidden"] == "1000");
  EXPECT(dict["name"] == "fc1");

  EXPECT(InitThrowsParamError(Kwargs{{"num_hidden", "1001"}}));
  EXPECT(InitThrowsParamError(Kwargs{{"num_hidden", "-1"}}));
  EXPECT(InitThrowsParamError(Kwargs{{"num_hidden", "12abc"}}));
  EXPECT(InitThrowsParamError(Kwargs{{"learning_rate", "0.5"}}));
  EXPECT(InitThrowsParamError(
      Kwargs{{"num_hidden", "1"}, {"momentum", "0.9"}}));
}

static void TestStreams() {
  std::string buf;
  {
    dmlc::MemoryStringStream out(&buf);
    // The typed Write and Read are dmlc::Stream's; the memory streams
    // override only the byte-level overloads, which hide them.
    dmlc::Stream* strm = &out;
    strm->Write(std::vector<int>{3, -1, 7});
    strm->Write(std::string("dmlc"));
  }
  // Each container is a little-endian uint64_t length, then its elements.
  EXPECT(buf.size() == 8 + 3 * sizeof(int) + 8 + 4);
  EXPECT(buf[0] == 3 && buf[1] == 0 && buf[7] == 0);
  EXPECT(buf.compare(buf.size() - 4, 4, "dmlc") == 0);

  dmlc::MemoryFixedSizeStream in(&buf[0], buf.size());
  dmlc::Stream* strm = &in;
  std::vector<int> v;
  std::string s;
  EXPECT(strm->Read(&v));
  EXPECT(strm->Read(&s));
  EXPECT((v == std::vector<int>{3, -1, 7}));
  EXPECT(s == "dmlc");
  EXPECT(in.Tell() == buf.size());

  // Reading past the end of a fixed-size stream fails a CHECK, which
  // throws dmlc::Error by default.
  bool threw = false;
  try {
    char extra;
    in.Read(&extra, 1);
  } catch (const dmlc::Error&) {
    threw = true;
  }
  EXPECT(threw);
}

static void TestRecordIO() {
  const uint32_t magic = dmlc::RecordIOWriter::kMagic;
  std::string with_magic = "abcd";
  with_magic.append(reinterpret_cast<const char*>(&magic), sizeof(magic));
  with_magic += "ef";

  std::string buf;
  {
    dmlc::MemoryStringStream out(&buf);
    dmlc::RecordIOWriter writer(&out);
    writer.WriteRecord(std::string("hello"));
    writer.WriteRecord(with_magic);
    EXPECT(writer.except_counter() == 1);
  }
  // "hello": header 8 + 5 bytes padded to 8; with_magic splits at the
  // embedded magic number into "abcd" (8 + 4) and "ef" (8 + 2 padded to 4).
  EXPECT(buf.size() == 16 + 12 + 12);

  dmlc::MemoryStringStream in(&buf);
  dmlc::RecordIOReader reader(&in);
  std::string rec;
  EXPECT(reader.NextRecord(&rec) && rec == "hello");
  EXPECT(reader.NextRecord(&rec) && rec == with_magic);
  EXPECT(!reader.NextRecord(&rec));
}

static void TestConfig() {
  std::istringstream text(
      "num_round = 10\n"
      "# a comment line\n"
      "eta = 0.3  # trailing comment\n"
      "name = \"two words\"\n"
      "eta = 0.1\n");
  dmlc::Config cfg(text);
  EXPECT(cfg.GetParam("num_round") == "10");
  EXPECT(cfg.GetParam("eta") == "0.1");
  EXPECT(cfg.GetParam("name") == "two words");
  EXPECT(cfg.IsGenuineString("name"));
  EXPECT(!cfg.IsGenuineString("num_round"));
  EXPECT(cfg.ToProtoString() ==
         "num_round : 10\nname : \"two words\"\neta : 0.1\n");

  std::istringstream multi("k = 1\nk = 2\n");
  dmlc::Config all(multi, true);
  std::vector<std::string> values;
  for (dmlc::Config::ConfigIterator it = all.begin(); it != all.end(); ++it) {
    values.push_back((*it).second);
  }
  EXPECT((values == std::vector<std::string>{"1", "2"}));

  std::string what;
  try {
    cfg.GetParam("missing");
  } catch (const dmlc::Error& e) {
    what = e.what();
  }
  EXPECT(what.find("key \"missing\" not found in configure") !=
         std::string::npos);
}

static void TestCheck() {
  std::string what;
  try {
    CHECK_EQ(2 + 2, 5) << "arithmetic";
  } catch (const dmlc::Error& e) {
    what = e.what();
  }
  EXPECT(what.find("Check failed: 2 + 2 == 5 (4 vs. 5) : arithmetic") !=
         std::string::npos);
}

static void TestFiles() {
  std::string path;
  {
    dmlc::TemporaryDirectory tmp;
    path = tmp.path + "/train.libsvm";
    {
      std::unique_ptr<dmlc::Stream> out(
          dmlc::Stream::Create(path.c_str(), "w"));
      const char data[] = "1 0:1.5 3:2\n0 1:-4\n";
      out->Write(data, sizeof(data) - 1);
    }

    std::unique_ptr<dmlc::InputSplit> split(
        dmlc::InputSplit::Create(path.c_str(), 0, 1, "text"));
    std::vector<std::string> records;
    dmlc::InputSplit::Blob blob;
    while (split->NextRecord(&blob)) {
      records.push_back(
          std::string(static_cast<const char*>(blob.dptr), blob.size));
    }
    // A record spans its line and line ending; the splitter overwrites the
    // ending with '\0' when another record follows in the chunk.
    EXPECT((records ==
            std::vector<std::string>{std::string("1 0:1.5 3:2\0", 12),
                                     "0 1:-4\n"}));

    std::unique_ptr<dmlc::Parser<uint32_t> > parser(
        dmlc::Parser<uint32_t>::Create(path.c_str(), 0, 1, "libsvm"));
    std::vector<float> labels;
    std::vector<uint32_t> index;
    std::vector<float> value;
    std::vector<size_t> row_length;
    while (parser->Next()) {
      const dmlc::RowBlock<uint32_t>& block = parser->Value();
      for (size_t r = 0; r < block.size; ++r) {
        dmlc::Row<uint32_t> row = block[r];
        labels.push_back(row.get_label());
        row_length.push_back(row.length);
        for (size_t k = 0; k < row.length; ++k) {
          index.push_back(row.get_index(k));
          value.push_back(row.get_value(k));
        }
      }
    }
    EXPECT((labels == std::vector<float>{1.0f, 0.0f}));
    EXPECT((row_length == std::vector<size_t>{2, 1}));
    EXPECT((index == std::vector<uint32_t>{0, 3, 1}));
    EXPECT((value == std::vector<float>{1.5f, 2.0f, -4.0f}));
  }
  // The directory and its file are gone once tmp goes out of scope.
  EXPECT(!std::ifstream(path.c_str()).good());
}

static void TestThreadedIter() {
  int next = 0;
  dmlc::ThreadedIter<int> iter(2);
  iter.Init(
      [&next](int** cell) {
        if (next == 5) return false;
        if (*cell == nullptr) *cell = new int();
        **cell = next * next;
        ++next;
        return true;
      },
      [&next]() { next = 0; });
  std::vector<int> squares;
  while (iter.Next()) squares.push_back(iter.Value());
  EXPECT((squares == std::vector<int>{0, 1, 4, 9, 16}));
  iter.BeforeFirst();
  int sum = 0;
  while (iter.Next()) sum += iter.Value();
  EXPECT(sum == 30);
}

int main() {
  TestParameter();
  TestStreams();
  TestRecordIO();
  TestConfig();
  TestCheck();
  TestFiles();
  TestThreadedIter();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
