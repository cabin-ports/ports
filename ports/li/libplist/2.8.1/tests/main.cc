// Builds a dictionary with libplist++'s PList classes, round-trips it
// through XML and the binary format, checks the values through the C++ and
// C APIs, and checks that a clone is independent of its source.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <plist/plist++.h>

static int fail(const char* what) {
  std::fprintf(stderr, "%s\n", what);
  return 1;
}

static bool check(PList::Dictionary& dict) {
  PList::String* name = dict.Get<PList::String>("name");
  PList::Integer* count = dict.Get<PList::Integer>("count");
  PList::Real* ratio = dict.Get<PList::Real>("ratio");
  PList::Data* blob = dict.Get<PList::Data>("blob");
  PList::Array* items = dict.Get<PList::Array>("items");
  if (name == nullptr || count == nullptr || ratio == nullptr ||
      blob == nullptr || items == nullptr) {
    return false;
  }
  const std::vector<char> expected_blob = {'\0', '\x01', '\x7f', '\xff'};
  PList::String* second =
      items->size() == 2 ? dynamic_cast<PList::String*>((*items)[1]) : nullptr;
  // The PList node wraps the same plist_t the C API reads.
  int64_t c_count = 0;
  plist_get_int_val(count->GetPlist(), &c_count);
  return name->GetValue() == "Cabin" && count->GetValue() == -7 &&
         count->isNegative() && c_count == -7 && ratio->GetValue() == 1.5 &&
         blob->GetValue() == expected_blob && second != nullptr &&
         second->GetValue() == "two";
}

int main() {
  PList::Dictionary dict;
  dict.Set("name", PList::String("Cabin"));
  dict.Set("count", PList::Integer(static_cast<int64_t>(-7)));
  dict.Set("ratio", PList::Real(1.5));
  dict.Set("blob", PList::Data(std::vector<char>{'\0', '\x01', '\x7f', '\xff'}));
  PList::Array items;
  items.Append(PList::Boolean(true));
  items.Append(PList::String("two"));
  dict.Set("items", items);
  if (!check(dict)) return fail("built dictionary");

  const std::string xml = dict.ToXml();
  if (xml.find("<string>Cabin</string>") == std::string::npos) {
    return fail("XML text");
  }
  std::unique_ptr<PList::Structure> from_xml(PList::Structure::FromXml(xml));
  auto* xml_dict = dynamic_cast<PList::Dictionary*>(from_xml.get());
  if (xml_dict == nullptr || !check(*xml_dict)) return fail("XML round trip");

  const std::vector<char> bin = dict.ToBin();
  plist_format_t format = PLIST_FORMAT_NONE;
  std::unique_ptr<PList::Structure> from_bin(
      PList::Structure::FromMemory(bin, &format));
  auto* bin_dict = dynamic_cast<PList::Dictionary*>(from_bin.get());
  if (format != PLIST_FORMAT_BINARY || bin_dict == nullptr ||
      !check(*bin_dict)) {
    return fail("binary round trip");
  }

  std::unique_ptr<PList::Node> clone(dict.Clone());
  dict.Get<PList::String>("name")->SetValue("changed");
  if (dict.Get<PList::String>("name")->GetValue() != "changed") {
    return fail("SetValue");
  }
  auto* cloned = dynamic_cast<PList::Dictionary*>(clone.get());
  if (cloned == nullptr || !check(*cloned)) return fail("clone changed");

  std::printf("libplist++: XML, binary and clone checks ok\n");
  return 0;
}
