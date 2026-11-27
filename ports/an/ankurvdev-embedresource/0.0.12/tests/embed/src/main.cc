// res.cpp is what the build script had embedresource write from
// hello.txt ("hello\n"), blob!two.bin (0x00 0xff), and @list.txt, which
// names notes-1.txt ("abc").
#include <EmbeddedResource.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

DECLARE_RESOURCE_COLLECTION(res);
DECLARE_RESOURCE(res, hello_txt);
DECLARE_RESOURCE(res, two_bin);
DECLARE_RESOURCE(res, notes_1_txt);

int main() {
  // A path's resource is named after its file.
  ResourceLoader hello(LOAD_RESOURCE(res, hello_txt));
  if (hello.name() != L"hello.txt" || hello.string() != "hello\n") {
    return 1;
  }

  // `name!path` names the resource; its symbol still comes from the file.
  ResourceLoader blob(LOAD_RESOURCE(res, two_bin));
  const std::span<const std::uint8_t> bytes = blob.data<std::uint8_t>();
  if (blob.name() != L"blob" || bytes.size() != 2 || bytes[0] != 0x00 || bytes[1] != 0xff) {
    return 2;
  }

  // `@file` adds the resources its lines name.
  ResourceLoader notes(LOAD_RESOURCE(res, notes_1_txt));
  if (notes.name() != L"notes-1.txt" || notes.string() != "abc") {
    return 3;
  }

  // The collection yields every resource once, in the order of the
  // tool's arguments.
  const std::wstring_view names[] = {L"hello.txt", L"blob", L"notes-1.txt"};
  std::size_t count = 0;
  auto collection = LOAD_RESOURCE_COLLECTION(res);
  for (auto const r : collection) {
    if (count == 3 || r.name() != names[count]) {
      return 4;
    }
    ++count;
  }
  return count == 3 ? 0 : 5;
}
