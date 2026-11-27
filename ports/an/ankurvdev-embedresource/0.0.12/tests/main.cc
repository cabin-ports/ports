// Two resources defined by hand in the shape embedresource writes them, so
// the header's macros, loaders, and conversions run without the tool;
// tests/embed runs the tool.
#include <EmbeddedResource.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

namespace EmbeddedResource::Data::demo::Resources::hello_txt {
static constexpr uint8_t kData[] = {0x68u, 0x65u, 0x6cu, 0x6cu, 0x6fu};
static constexpr std::wstring_view kName = L"hello.txt";
}  // namespace EmbeddedResource::Data::demo::Resources::hello_txt

namespace EmbeddedResource::Data::demo::Resources::blob {
static constexpr uint8_t kData[] = {0x0u, 0xffu, 0x7fu};
static constexpr std::wstring_view kName = L"blob";
}  // namespace EmbeddedResource::Data::demo::Resources::blob

DECLARE_RESOURCE(demo, hello_txt);
DECLARE_RESOURCE(demo, blob);

DECLARE_RESOURCE(demo, hello_txt) {
  namespace r = EmbeddedResource::Data::demo::Resources::hello_txt;
  return EmbeddedResource::ABI::ResourceInfo{{r::kName.data(), r::kName.size()},
                                             {r::kData, std::size(r::kData)}};
}

DECLARE_RESOURCE(demo, blob) {
  namespace r = EmbeddedResource::Data::demo::Resources::blob;
  return EmbeddedResource::ABI::ResourceInfo{{r::kName.data(), r::kName.size()},
                                             {r::kData, std::size(r::kData)}};
}

namespace EmbeddedResource::Data::demo {
static constexpr EmbeddedResource::ABI::GetCollectionResourceInfo *const kTable[] = {
    EMBEDDEDRESOURCE_ABI_RESOURCE_FUNCNAME(demo, hello_txt, GetCollectionResourceInfo),
    EMBEDDEDRESOURCE_ABI_RESOURCE_FUNCNAME(demo, blob, GetCollectionResourceInfo)};
}  // namespace EmbeddedResource::Data::demo

DECLARE_RESOURCE_COLLECTION(demo);

DECLARE_RESOURCE_COLLECTION(demo) {
  return EmbeddedResource::ABI::Data<EmbeddedResource::ABI::GetCollectionResourceInfo *>{
      EmbeddedResource::Data::demo::kTable,
      std::size(EmbeddedResource::Data::demo::kTable)};
}

int main() {
  // ResourceLoader over one resource.
  ResourceLoader hello(LOAD_RESOURCE(demo, hello_txt));
  if (hello.name() != L"hello.txt" || hello.string() != "hello") {
    return 1;
  }
  ResourceLoader blob(LOAD_RESOURCE(demo, blob));
  if (blob.name() != L"blob" || blob.string() != std::string_view("\0\xff\x7f", 3)) {
    return 2;
  }

  // The conversions of a resource's raw data.
  const std::string copy = LOAD_RESOURCE(demo, blob).data;
  const std::string_view view = LOAD_RESOURCE(demo, hello_txt).data;
  if (copy != std::string("\0\xff\x7f", 3) || view != "hello") {
    return 3;
  }

  // The collection yields every resource once, in table order.
  const std::wstring_view names[] = {L"hello.txt", L"blob"};
  const std::size_t sizes[] = {5, 3};
  std::size_t count = 0;
  auto collection = LOAD_RESOURCE_COLLECTION(demo);
  for (auto const r : collection) {
    if (count == 2 || r.name() != names[count] || r.string().size() != sizes[count]) {
      return 4;
    }
    ++count;
  }
  return count == 2 ? 0 : 5;
}
