// Binary and JSON encodings with exact bytes, schema resolution, a
// deflate-compressed data file (the zlib dependency) and the formatted
// avro::Exception message (the fmt dependency).
#include <avro/Compiler.hh>
#include <avro/DataFile.hh>
#include <avro/Decoder.hh>
#include <avro/Encoder.hh>
#include <avro/Exception.hh>
#include <avro/Generic.hh>
#include <avro/GenericDatum.hh>
#include <avro/Stream.hh>
#include <avro/ValidSchema.hh>

#include <cstdint>
#include <cstdio>
#include <iostream>
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

const char* const kPerson = R"({
  "type": "record", "name": "Person",
  "fields": [
    {"name": "id", "type": "int"},
    {"name": "name", "type": "string"}
  ]
})";

avro::GenericDatum person(const avro::ValidSchema& schema, int32_t id, const std::string& name) {
    avro::GenericDatum datum(schema);
    auto& rec = datum.value<avro::GenericRecord>();
    rec.field("id") = avro::GenericDatum(id);
    rec.field("name") = avro::GenericDatum(name);
    return datum;
}

}  // namespace

int main() {
    const avro::ValidSchema schema = avro::compileJsonSchemaFromString(kPerson);

    // Zigzag varints (27 -> 0x36, -1 -> 0x01, 64 -> 0x80 0x01) and a length-prefixed string.
    {
        auto out = avro::memoryOutputStream();
        auto enc = avro::binaryEncoder();
        enc->init(*out);
        enc->encodeInt(27);
        enc->encodeString("foo");
        enc->encodeLong(-1);
        enc->encodeLong(64);
        enc->flush();
        const std::vector<uint8_t> want = {0x36, 0x06, 'f', 'o', 'o', 0x01, 0x80, 0x01};
        check(*avro::snapshot(*out) == want, "binary encoding bytes");
    }

    // Generic record: binary round trip, with the exact encoding.
    {
        auto out = avro::memoryOutputStream();
        auto enc = avro::binaryEncoder();
        enc->init(*out);
        avro::encode(*enc, person(schema, 27, "foo"));
        enc->flush();
        const std::vector<uint8_t> want = {0x36, 0x06, 'f', 'o', 'o'};
        check(*avro::snapshot(*out) == want, "record bytes");

        auto in = avro::memoryInputStream(*out);
        auto dec = avro::binaryDecoder();
        dec->init(*in);
        avro::GenericDatum back(schema);
        avro::decode(*dec, back);
        const auto& rec = back.value<avro::GenericRecord>();
        check(rec.field("id").value<int32_t>() == 27, "decoded id");
        check(rec.field("name").value<std::string>() == "foo", "decoded name");
    }

    // JSON encoding of the same record.
    {
        auto out = avro::memoryOutputStream();
        auto enc = avro::jsonEncoder(schema);
        enc->init(*out);
        avro::encode(*enc, person(schema, 27, "foo"));
        enc->flush();
        auto bytes = avro::snapshot(*out);
        check(std::string(bytes->begin(), bytes->end()) == R"({"id":27,"name":"foo"})", "json encoding");
    }

    // Schema resolution: the reader's new field takes its default.
    {
        const avro::ValidSchema reader = avro::compileJsonSchemaFromString(R"({
          "type": "record", "name": "Person",
          "fields": [
            {"name": "id", "type": "int"},
            {"name": "name", "type": "string"},
            {"name": "city", "type": "string", "default": "Oslo"}
          ]
        })");
        auto out = avro::memoryOutputStream();
        auto enc = avro::binaryEncoder();
        enc->init(*out);
        avro::encode(*enc, person(schema, 5, "bar"));
        enc->flush();

        auto in = avro::memoryInputStream(*out);
        auto dec = avro::resolvingDecoder(schema, reader, avro::binaryDecoder());
        dec->init(*in);
        avro::GenericDatum back(reader);
        avro::decode(*dec, back);
        const auto& rec = back.value<avro::GenericRecord>();
        check(rec.field("id").value<int32_t>() == 5, "resolved id");
        check(rec.field("name").value<std::string>() == "bar", "resolved name");
        check(rec.field("city").value<std::string>() == "Oslo", "resolved default");
    }

    // Data file with the deflate codec: smaller than the raw records, same content back.
    {
        constexpr int kCount = 1000;
        auto out = avro::memoryOutputStream();
        avro::OutputStream* raw = out.get();
        avro::DataFileWriter<avro::GenericDatum> writer(std::move(out), schema, 16 * 1024,
                                                        avro::DEFLATE_CODEC);
        for (int i = 0; i < kCount; ++i) {
            writer.write(person(schema, i, "the same name every time"));
        }
        writer.flush();  // close() would free the stream snapshot() reads
        auto file = avro::snapshot(*raw);
        check(file->size() < kCount * 10, "deflate shrinks the file");

        avro::DataFileReader<avro::GenericDatum> reader(
            avro::memoryInputStream(file->data(), file->size()), schema);
        avro::GenericDatum datum(schema);
        int n = 0;
        while (reader.read(datum)) {
            const auto& rec = datum.value<avro::GenericRecord>();
            check(rec.field("id").value<int32_t>() == n, "data file id");
            check(rec.field("name").value<std::string>() == "the same name every time",
                  "data file name");
            ++n;
        }
        check(n == kCount, "data file record count");
    }

    // Exception formats its message with fmt.
    try {
        avro::compileJsonSchemaFromString(
            R"({"type": "record", "name": "R", "fields": [{"name": "a", "type": "bogus"}]})");
        check(false, "unknown type must throw");
    } catch (const avro::Exception& e) {
        check(std::string(e.what()) == "Unknown type: bogus", "exception message");
    }

    return failures == 0 ? 0 : 1;
}
