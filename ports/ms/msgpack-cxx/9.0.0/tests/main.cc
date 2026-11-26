#include <msgpack.hpp>

#include <boost/fusion/include/vector.hpp>
#include <boost/optional.hpp>
#include <boost/utility/string_ref.hpp>

#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "msgpack-cxx: %s\n", what);
    ++failures;
  }
}

template <typename T>
std::string pack_bytes(const T& v) {
  msgpack::sbuffer sb;
  msgpack::pack(sb, v);
  return std::string(sb.data(), sb.size());
}

template <typename T>
T round_trip(const T& v) {
  std::string s = pack_bytes(v);
  msgpack::object_handle oh = msgpack::unpack(s.data(), s.size());
  return oh.get().as<T>();
}

struct Inner {
  int x;
  MSGPACK_DEFINE(x);
};

struct Outer {
  int a;
  std::string b;
  Inner in;
  std::vector<int> v;
  MSGPACK_DEFINE(a, b, in, v);
};

}  // namespace

int main() {
  // Wire format against hand-computed MessagePack encodings.
  check(pack_bytes(300) == "\xcd\x01\x2c", "uint16 300");
  check(pack_bytes(-1) == "\xff", "negative fixint");
  check(pack_bytes(-33) == "\xd0\xdf", "int8 -33");
  check(pack_bytes(65536u) == std::string("\xce\x00\x01\x00\x00", 5), "uint32");
  check(pack_bytes(1.5) == std::string("\xcb\x3f\xf8\0\0\0\0\0\0", 9), "float64");
  check(pack_bytes(true) == "\xc3" && pack_bytes(false) == "\xc2", "bool");
  check(pack_bytes(std::string("abc")) == "\xa3" "abc", "fixstr");
  check(pack_bytes(std::vector<int>{1, 2, 3}) == "\x93\x01\x02\x03", "fixarray");
  check(pack_bytes(std::map<std::string, int>{{"a", 1}}) == "\x81\xa1" "a\x01",
        "fixmap");
  check(pack_bytes(std::make_tuple(1, std::string("a"), true)) ==
            "\x93\x01\xa1" "a\xc3",
        "tuple");

  // A MSGPACK_DEFINE struct nested in another packs as an array of arrays.
  Outer o{7, "x", {-1}, {1, 2}};
  check(pack_bytes(o) == "\x94\x07\xa1" "x\x91\xff\x92\x01\x02", "struct");

  // Unpack and convert back to exact values.
  {
    std::string s = pack_bytes(o);
    msgpack::object_handle oh = msgpack::unpack(s.data(), s.size());
    Outer r = oh.get().as<Outer>();
    check(r.a == 7 && r.b == "x" && r.in.x == -1 && r.v == o.v, "struct round trip");
  }
  check(round_trip(std::vector<int>{1, 2, 3}) == std::vector<int>({1, 2, 3}), "vector");
  check(round_trip(std::map<std::string, int>{{"k", -5}, {"l", 70000}}) ==
            (std::map<std::string, int>{{"k", -5}, {"l", 70000}}),
        "map");
  check(round_trip(std::make_tuple(1, std::string("a"), 2.5)) ==
            std::make_tuple(1, std::string("a"), 2.5),
        "tuple round trip");
  check(round_trip(std::string(300, 'z')) == std::string(300, 'z'), "str16");

  // An object built in a zone stays valid and packs like the plain value.
  {
    msgpack::zone z;
    msgpack::object obj(std::vector<std::string>{"hello", "zone"}, z);
    check(obj.type == msgpack::type::ARRAY && obj.via.array.size == 2, "zone object");
    std::string s = pack_bytes(obj);
    check(s == "\x92\xa5hello\xa4zone", "zone object bytes");
    msgpack::object_handle oh = msgpack::unpack(s.data(), s.size());
    check(oh.get().as<std::vector<std::string>>() ==
              std::vector<std::string>({"hello", "zone"}),
          "object_handle");
    check(oh.get().via.array.ptr[1].as<std::string>() == "zone", "array element");
  }

  // A mismatched convert reports a type error.
  {
    std::string s = pack_bytes(std::string("text"));
    msgpack::object_handle oh = msgpack::unpack(s.data(), s.size());
    bool thrown = false;
    try {
      (void)oh.get().as<int>();
    } catch (const msgpack::type_error&) {
      thrown = true;
    }
    check(thrown, "as<int> of a string throws type_error");
    int i = 0;
    thrown = false;
    try {
      oh.get().convert(i);
    } catch (const msgpack::type_error&) {
      thrown = true;
    }
    check(thrown, "convert(int) of a string throws type_error");
  }

  // Boost adaptors, on by default.
  check(pack_bytes(boost::optional<int>()) == "\xc0", "empty optional");
  check(pack_bytes(boost::optional<int>(5)) == "\x05", "optional");
  {
    std::string s = pack_bytes(boost::optional<int>());
    msgpack::object_handle oh = msgpack::unpack(s.data(), s.size());
    check(!oh.get().as<boost::optional<int>>(), "nil to empty optional");
  }
  check(pack_bytes(boost::fusion::vector<int, std::string>(1, "q")) ==
            "\x92\x01\xa1" "q",
        "fusion vector");
  check(pack_bytes(boost::string_ref("hi")) == "\xa2hi", "string_ref");
  {
    msgpack::type::variant v = 3;
    check(round_trip(v) == v && pack_bytes(v) == "\x03", "variant");
  }

  return failures == 0 ? 0 : 1;
}
