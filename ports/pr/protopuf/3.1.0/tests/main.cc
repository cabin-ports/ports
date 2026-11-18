#include <protopuf/array.h>
#include <protopuf/message.h>

#include <array>
#include <cstddef>
#include <cstdio>

using namespace pp;

// README.md's example.
using Student = message<uint32_field<"id", 1>, string_field<"name", 3>>;
using Class = message<string_field<"name", 8>, message_field<"students", 3, Student, repeated>>;

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The wire encoding, worked out by hand.  protopuf writes fields in declaration
// order: field 8 "class 101", then the three students as field 3, each with id
// (field 1) and name (field 3).
static constexpr unsigned char encoded[] = {
    0x42, 0x09, 'c', 'l', 'a', 's', 's', ' ', '1', '0', '1',
    0x1a, 0x08, 0x08, 0xc8, 0x03, 0x1a, 0x03, 't', 'o', 'm',
    0x1a, 0x0b, 0x08, 0xc0, 0xc4, 0x07, 0x1a, 0x05, 'j', 'e', 'r', 'r', 'y',
    0x1a, 0x09, 0x08, 0x7b, 0x1a, 0x05, 't', 'w', 'i', 'c', 'e',
};

int main() {
    Student twice{123, "twice"}, tom{456, "tom"}, jerry{123456, "jerry"};
    Class myClass{"class 101", {tom, jerry}};
    myClass["students"_f].push_back(twice);

    std::array<std::byte, 64> buffer{};
    auto result = message_coder<Class>::encode(myClass, buffer);
    check(result.has_value(), "encode");
    if (!result.has_value())
        return 1;
    const auto& bufferEnd = *result;
    check(begin_diff(bufferEnd, buffer) == 45, "encoded length");
    bool same = true;
    for (std::size_t i = 0; i < sizeof encoded; ++i)
        same = same && buffer[i] == std::byte{encoded[i]};
    check(same, "encoded bytes");

    auto result2 = message_coder<Class>::decode(buffer);
    check(result2.has_value(), "decode");
    if (!result2.has_value())
        return 1;
    const auto& [yourClass, bufferEnd2] = *result2;
    check(yourClass["name"_f] == "class 101", "name");
    check(yourClass["students"_f].size() == 3, "student count");
    if (yourClass["students"_f].size() != 3)
        return 1;
    check(yourClass["students"_f][2]["name"_f] == "twice", "third student's name");
    check(yourClass["students"_f][2]["id"_f] == 123u, "third student's id");
    check(yourClass["students"_f][1] == (Student{123456, "jerry"}), "second student");
    check(yourClass == myClass, "decoded message");
    check(begin_diff(bufferEnd2, bufferEnd) == 0, "decode consumed the encoding");

    if (failures != 0)
        return 1;
    std::puts("protopuf consumer test passed");
    return 0;
}
