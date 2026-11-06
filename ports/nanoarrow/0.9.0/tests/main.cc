// The C++ consumer of nanoarrow.hpp: the owning wrappers releasing
// exactly once across moves, NANOARROW_THROW_NOT_OK raising
// nanoarrow::Exception, ViewArrayAs over an int32 array with nulls and an
// offset, ViewArrayAsBytes with the _asv literal, a wrapped buffer that
// shares its owner, and a VectorArrayStream drained through
// ViewArrayStream.
#include <nanoarrow/nanoarrow.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

std::string schema_string(const ArrowSchema* schema) {
  char out[64];
  ArrowSchemaToString(schema, out, sizeof(out), 1);
  return out;
}

int releases = 0;

void count_release(ArrowSchema* schema) {
  ++releases;
  schema->release = nullptr;
}

void unique_ownership() {
  ArrowSchema raw;
  std::memset(&raw, 0, sizeof(raw));
  raw.format = "i";
  raw.release = &count_release;
  {
    nanoarrow::UniqueSchema first(&raw);
    check(raw.release == nullptr, "UniqueSchema takes ownership of a raw schema");
    nanoarrow::UniqueSchema second(std::move(first));
    check(first->release == nullptr && second->release != nullptr,
          "a move leaves the source released");
    nanoarrow::UniqueSchema third;
    third = std::move(second);
    check(third->release == &count_release, "move assignment carries the schema");
    check(releases == 0, "nothing is released while an owner remains");
  }
  check(releases == 1, "the schema is released exactly once");

  nanoarrow::UniqueSchema schema;
  NANOARROW_THROW_NOT_OK(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_STRING));
  check(schema_string(schema.get()) == "string", "a string schema");
  schema.reset();
  check(schema->release == nullptr, "reset releases the schema");
}

// [10, null, 30, 40, null, 60]
nanoarrow::UniqueArray make_ints() {
  nanoarrow::UniqueArray array;
  NANOARROW_THROW_NOT_OK(ArrowArrayInitFromType(array.get(), NANOARROW_TYPE_INT32));
  NANOARROW_THROW_NOT_OK(ArrowArrayStartAppending(array.get()));
  const int32_t values[] = {10, 0, 30, 40, 0, 60};
  for (int i = 0; i < 6; ++i) {
    if (i == 1 || i == 4) {
      NANOARROW_THROW_NOT_OK(ArrowArrayAppendNull(array.get(), 1));
    } else {
      NANOARROW_THROW_NOT_OK(ArrowArrayAppendInt(array.get(), values[i]));
    }
  }
  NANOARROW_THROW_NOT_OK(ArrowArrayFinishBuildingDefault(array.get(), nullptr));
  return array;
}

std::vector<int32_t> ints_or(const ArrowArray* array, int32_t null_value) {
  std::vector<int32_t> out;
  for (nanoarrow::ViewArrayAs<int32_t>::value_type value :
       nanoarrow::ViewArrayAs<int32_t>(array)) {
    out.push_back(value.value_or(null_value));
  }
  return out;
}

void views() {
  nanoarrow::UniqueArray ints = make_ints();
  check(ints->null_count == 2, "two nulls");
  check(ints_or(ints.get(), -1) == std::vector<int32_t>({10, -1, 30, 40, -1, 60}),
        "ViewArrayAs yields empty values for nulls");

  // A slice is an offset and a length, with its null count now unknown.
  ints->offset = 2;
  ints->length = 3;
  ints->null_count = -1;
  check(ints_or(ints.get(), -1) == std::vector<int32_t>({30, 40, -1}),
        "ViewArrayAs honors the array's offset");

  using namespace nanoarrow::literals;
  nanoarrow::UniqueArray strings;
  NANOARROW_THROW_NOT_OK(ArrowArrayInitFromType(strings.get(), NANOARROW_TYPE_STRING));
  NANOARROW_THROW_NOT_OK(ArrowArrayStartAppending(strings.get()));
  NANOARROW_THROW_NOT_OK(ArrowArrayAppendString(strings.get(), "alpha"_asv));
  NANOARROW_THROW_NOT_OK(ArrowArrayAppendNull(strings.get(), 1));
  NANOARROW_THROW_NOT_OK(ArrowArrayAppendString(strings.get(), "beta"_asv));
  NANOARROW_THROW_NOT_OK(ArrowArrayFinishBuildingDefault(strings.get(), nullptr));

  nanoarrow::UniqueArrayView view;
  ArrowArrayViewInitFromType(view.get(), NANOARROW_TYPE_STRING);
  NANOARROW_THROW_NOT_OK(ArrowArrayViewSetArray(view.get(), strings.get(), nullptr));
  std::vector<std::string> seen;
  for (nanoarrow::ViewArrayAsBytes<32>::value_type value :
       nanoarrow::ViewArrayAsBytes<32>(view.get())) {
    seen.push_back(value ? std::string((*value).data,
                                       static_cast<size_t>((*value).size_bytes))
                         : std::string("<null>"));
  }
  check(seen == std::vector<std::string>({"alpha", "<null>", "beta"}),
        "ViewArrayAsBytes reads strings and nulls");
  check(ArrowArrayViewGetStringUnsafe(view.get(), 2) == "beta"_asv,
        "ArrowStringView equality with the _asv literal");
  check(!(ArrowArrayViewGetStringUnsafe(view.get(), 0) == "alphabet"_asv),
        "ArrowStringView equality compares sizes");
}

void exceptions() {
  nanoarrow::UniqueArray small;
  NANOARROW_THROW_NOT_OK(ArrowArrayInitFromType(small.get(), NANOARROW_TYPE_INT8));
  NANOARROW_THROW_NOT_OK(ArrowArrayStartAppending(small.get()));
  bool thrown = false;
  try {
    NANOARROW_THROW_NOT_OK(ArrowArrayAppendInt(small.get(), 1000));
  } catch (const nanoarrow::Exception& e) {
    thrown = true;
    const std::string expected = "ArrowArrayAppendInt(small.get(), 1000) failed with errno " +
                                 std::to_string(EINVAL);
    check(std::string(e.what()).compare(0, expected.size(), expected) == 0,
          "the exception names the expression and errno");
  }
  check(thrown, "an out-of-range int8 append throws");
  check(small->length == 0, "the failed append adds nothing");
}

void wrapped_buffers() {
  std::shared_ptr<std::vector<uint8_t>> owner =
      std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>({1, 2, 3}));
  {
    nanoarrow::UniqueBuffer buffer;
    nanoarrow::BufferInitWrapped(buffer.get(), owner, owner->data(),
                                 static_cast<int64_t>(owner->size()));
    check(buffer->data == owner->data() && buffer->size_bytes == 3,
          "a wrapped buffer points at its owner's bytes");
    check(owner.use_count() == 2, "the wrapped buffer holds a reference");
  }
  check(owner.use_count() == 1, "releasing the buffer drops its reference");

  nanoarrow::UniqueBuffer sequence;
  nanoarrow::BufferInitSequence(sequence.get(), std::string("sequence"));
  check(sequence->size_bytes == 8 && std::memcmp(sequence->data, "sequence", 8) == 0,
        "BufferInitSequence wraps a string's bytes");
}

void streams() {
  nanoarrow::UniqueSchema schema;
  NANOARROW_THROW_NOT_OK(ArrowSchemaInitFromType(schema.get(), NANOARROW_TYPE_INT32));
  std::vector<nanoarrow::UniqueArray> batches;
  batches.push_back(make_ints());
  batches.push_back(make_ints());
  batches.back()->length = 3;  // [10, null, 30]
  batches.back()->null_count = -1;

  nanoarrow::UniqueArrayStream stream;
  nanoarrow::VectorArrayStream(schema.get(), std::move(batches)).ToArrayStream(stream.get());
  check(schema->release == nullptr, "the stream owns the schema");

  nanoarrow::UniqueSchema got;
  NANOARROW_THROW_NOT_OK(ArrowArrayStreamGetSchema(stream.get(), got.get(), nullptr));
  check(schema_string(got.get()) == "int32", "the stream's schema");

  int64_t sum = 0;
  int64_t rows = 0;
  nanoarrow::ViewArrayStream arrays(stream.get());
  for (ArrowArray& array : arrays) {
    rows += array.length;
    for (nanoarrow::ViewArrayAs<int32_t>::value_type value :
         nanoarrow::ViewArrayAs<int32_t>(&array)) {
      sum += value.value_or(0);
    }
  }
  check(arrays.code() == NANOARROW_OK, "the stream ends without an error");
  check(arrays.count() == 2, "two batches");
  check(rows == 9, "nine rows");
  check(sum == 140 + 40, "the sum of both batches");
}

}  // namespace

int main() {
  try {
    unique_ownership();
    views();
    exceptions();
    wrapped_buffers();
    streams();
  } catch (const nanoarrow::Exception& e) {
    std::fprintf(stderr, "FAIL: unexpected exception: %s\n", e.what());
    ok = false;
  }
  std::printf("nanoarrow (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
