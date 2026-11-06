// Exercises the flatbuffers runtime library without flatc: a schema and
// JSON parsed at run time, the binary schema it serializes, reflection
// reads and verification, JSON text generation, hand-built buffers and
// FlexBuffers.  Kept C++11-clean.
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/flexbuffers.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>
#include <flatbuffers/util.h>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

static const char kSchema[] =
    "namespace cabin;\n"
    "enum Color : byte { Red = 0, Green, Blue = 8 }\n"
    "struct Vec3 { x: float; y: float; z: float; }\n"
    "table Sword { damage: short; }\n"
    "table Shield { armor: int; }\n"
    "union Gear { Sword, Shield }\n"
    "table Monster {\n"
    "  pos: Vec3;\n"
    "  hp: short = 100;\n"
    "  name: string (required);\n"
    "  inventory: [ubyte];\n"
    "  color: Color = Blue;\n"
    "  gear: [Gear];\n"
    "  ratio: float;\n"
    "}\n"
    "root_type Monster;\n"
    "file_identifier \"CABN\";\n";

// Strict JSON, as IDLOptions::strict_json requires.
static const char kJson[] =
    "{ \"pos\": { \"x\": 1, \"y\": 2, \"z\": 3 }, \"name\": \"Orc\",\n"
    "  \"inventory\": [1, 2, 3], \"color\": \"Red\",\n"
    "  \"gear_type\": [\"Sword\", \"Shield\"],\n"
    "  \"gear\": [{ \"damage\": 7 }, { \"armor\": 9 }], \"ratio\": 1.5 }";

// What flatc emits for a bit_flags enum.
enum class Access : uint8_t { None = 0, Read = 1, Write = 2 };
FLATBUFFERS_DEFINE_BITMASK_OPERATORS(Access, uint8_t)

static const reflection::Field& field(const reflection::Object& object,
                                      const char* name) {
  const reflection::Field* f = object.fields()->LookupByKey(name);
  CHECK(f != nullptr);
  return *f;
}

static const reflection::Object& object(const reflection::Schema& schema,
                                        const char* name) {
  const reflection::Object* o = schema.objects()->LookupByKey(name);
  CHECK(o != nullptr);
  return *o;
}

// A Monster holding a union vector of `types` over one Sword, built by
// hand from the field offsets the binary schema records.
static std::vector<uint8_t> gear_buffer(const reflection::Schema& schema,
                                        const std::vector<uint8_t>& types) {
  const reflection::Object& monster = *schema.root_table();
  flatbuffers::FlatBufferBuilder b;
  const flatbuffers::Offset<flatbuffers::String> name = b.CreateString("x");
  const flatbuffers::uoffset_t start = b.StartTable();
  b.AddElement<int16_t>(field(object(schema, "cabin.Sword"), "damage").offset(),
                        5, 0);
  const flatbuffers::Offset<void> sword(b.EndTable(start));
  const flatbuffers::Offset<flatbuffers::Vector<uint8_t>> type_vec =
      b.CreateVector(types);
  const flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<void>>>
      value_vec = b.CreateVector(std::vector<flatbuffers::Offset<void>>(1, sword));
  const flatbuffers::uoffset_t m = b.StartTable();
  b.AddOffset(field(monster, "name").offset(), name);
  b.AddOffset(field(monster, "gear_type").offset(), type_vec);
  b.AddOffset(field(monster, "gear").offset(), value_vec);
  b.Finish(flatbuffers::Offset<flatbuffers::Table>(b.EndTable(m)), "CABN");
  return std::vector<uint8_t>(b.GetBufferPointer(),
                              b.GetBufferPointer() + b.GetSize());
}

static bool verify(const reflection::Schema& schema,
                   const std::vector<uint8_t>& buf) {
  return flatbuffers::Verify(schema, *schema.root_table(), buf.data(),
                             buf.size());
}

int main() {
  CHECK(std::strcmp(flatbuffers::FLATBUFFERS_VERSION(), "25.12.19") == 0);
  CHECK(FLATBUFFERS_VERSION_MAJOR == 25 && FLATBUFFERS_VERSION_MINOR == 12 &&
        FLATBUFFERS_VERSION_REVISION == 19);

  // Schema and JSON to binary.
  flatbuffers::IDLOptions opts;
  opts.strict_json = true;
  opts.indent_step = -1;
  flatbuffers::Parser parser(opts);
  CHECK(parser.Parse(kSchema));
  CHECK(parser.Parse(kJson));
  const std::vector<uint8_t> data(
      parser.builder_.GetBufferPointer(),
      parser.builder_.GetBufferPointer() + parser.builder_.GetSize());
  CHECK(flatbuffers::BufferHasIdentifier(data.data(), "CABN"));

  // Binary back to JSON: enum and union type names, hp left at its default.
  std::string text;
  CHECK(flatbuffers::GenText(parser, data.data(), &text) == nullptr);
  CHECK(text ==
        "{\"pos\": {\"x\": 1.0,\"y\": 2.0,\"z\": 3.0},\"name\": \"Orc\","
        "\"inventory\": [1,2,3],\"color\": \"Red\","
        "\"gear_type\": [\"Sword\",\"Shield\"],"
        "\"gear\": [{\"damage\": 7},{\"armor\": 9}],\"ratio\": 1.5}");

  // The same JSON parses to the same bytes.
  flatbuffers::Parser reparser(opts);
  CHECK(reparser.Parse(kSchema));
  CHECK(reparser.Parse(text.c_str()));
  CHECK(reparser.builder_.GetSize() == data.size() &&
        std::memcmp(reparser.builder_.GetBufferPointer(), data.data(),
                    data.size()) == 0);

  // The binary schema, read through reflection.
  parser.Serialize();
  const std::vector<uint8_t> bfbs(
      parser.builder_.GetBufferPointer(),
      parser.builder_.GetBufferPointer() + parser.builder_.GetSize());
  flatbuffers::Verifier schema_verifier(bfbs.data(), bfbs.size());
  CHECK(reflection::VerifySchemaBuffer(schema_verifier));
  const reflection::Schema& schema = *reflection::GetSchema(bfbs.data());
  CHECK(schema.file_ident()->str() == "CABN");
  const reflection::Object& monster = *schema.root_table();
  CHECK(monster.name()->str() == "cabin.Monster");

  const flatbuffers::Table& root = *flatbuffers::GetAnyRoot(data.data());
  CHECK(flatbuffers::GetFieldI<int16_t>(root, field(monster, "hp")) == 100);
  CHECK(flatbuffers::GetFieldS(root, field(monster, "name"))->str() == "Orc");
  CHECK(flatbuffers::GetAnyFieldI(root, field(monster, "color")) == 0);
  CHECK(flatbuffers::GetFieldF<float>(root, field(monster, "ratio")) == 1.5f);
  const flatbuffers::Vector<uint8_t>* inventory =
      flatbuffers::GetFieldV<uint8_t>(root, field(monster, "inventory"));
  CHECK(inventory->size() == 3 && inventory->Get(0) == 1 &&
        inventory->Get(2) == 3);
  const flatbuffers::Struct* pos =
      flatbuffers::GetFieldStruct(root, field(monster, "pos"));
  CHECK(flatbuffers::GetAnyFieldF(*pos, field(object(schema, "cabin.Vec3"),
                                              "z")) == 3.0);

  // Verification accepts the parsed buffer and rejects broken ones.
  CHECK(verify(schema, data));
  CHECK(!verify(schema, std::vector<uint8_t>(data.begin(),
                                             data.begin() + data.size() / 2)));
  std::vector<uint8_t> bad_root = data;
  bad_root[1] = 0x7f;  // root offset far past the end
  CHECK(!verify(schema, bad_root));

  // A union vector whose type vector outruns its values.
  CHECK(verify(schema, gear_buffer(schema, std::vector<uint8_t>(1, 1))));
  CHECK(!verify(schema, gear_buffer(schema, std::vector<uint8_t>(2, 1))));

  // Parse errors.
  flatbuffers::Parser strict(opts);
  CHECK(strict.Parse(kSchema));
  CHECK(!strict.Parse("{ \"name\": \"x\", \"bogus\": 1 }"));
  CHECK(strict.error_.find("unknown field: bogus") != std::string::npos);
  flatbuffers::Parser missing(opts);
  CHECK(missing.Parse(kSchema));
  CHECK(!missing.Parse("{ \"hp\": 1 }"));
  CHECK(missing.error_.find("required field is missing: name") !=
        std::string::npos);

  // Bitmask operators.
  Access access = Access::Read;
  access |= Access::Write;
  CHECK(access == (Access::Read | Access::Write));
  access &= ~Access::Read;
  CHECK(access == Access::Write && !(access & Access::Read));

  // FlexBuffers from schemaless JSON.
  flexbuffers::Builder fbb;
  flatbuffers::Parser flex;
  CHECK(flex.ParseFlexBuffer("{ a: 7, s: \"hi\", v: [1.5, true] }", nullptr,
                             &fbb));
  const std::vector<uint8_t>& flexbuf = fbb.GetBuffer();
  CHECK(flexbuffers::VerifyBuffer(flexbuf.data(), flexbuf.size()));
  const flexbuffers::Map map = flexbuffers::GetRoot(flexbuf).AsMap();
  CHECK(map.size() == 3);
  CHECK(map["a"].AsInt64() == 7);
  CHECK(map["s"].AsString().str() == "hi");
  CHECK(map["v"].AsVector()[0].AsDouble() == 1.5);
  CHECK(map["v"].AsVector()[1].AsBool());

  // Numbers parse in the C locale whatever the global locale.
  float f = 0;
  CHECK(flatbuffers::StringToNumber("2.25", &f) && f == 2.25f);
#if FLATBUFFERS_LOCALE_INDEPENDENT
  static const char* const kCommaLocales[] = {"de_DE.UTF-8", "de_DE",
                                               "de-DE", "fr_FR.UTF-8"};
  for (const char* name : kCommaLocales) {
    if (!std::setlocale(LC_ALL, name)) continue;
    if (std::localeconv()->decimal_point[0] != ',') {
      std::setlocale(LC_ALL, "C");
      continue;
    }
    std::printf("checked under locale %s\n", name);
    CHECK(flatbuffers::StringToNumber("2.25", &f) && f == 2.25f);
    flatbuffers::Parser localized(opts);
    CHECK(localized.Parse(kSchema));
    CHECK(localized.Parse(kJson));
    CHECK(localized.builder_.GetSize() == data.size() &&
          std::memcmp(localized.builder_.GetBufferPointer(), data.data(),
                      data.size()) == 0);
    std::setlocale(LC_ALL, "C");
    break;
  }
#endif

  std::puts("ok");
  return 0;
}
