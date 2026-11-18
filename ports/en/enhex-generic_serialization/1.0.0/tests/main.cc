// As upstream's example.cpp does, for MSVC's std::getenv deprecation warning.
#define _CRT_SECURE_NO_WARNINGS

#include <gs/binary.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

// The types and serialize overloads of upstream's test_package/example.cpp.
struct Vector3 {
    float x, y, z;
};

struct A {
    int x = 0;
    float y = 0;
    char z[3];
    Vector3 vec3;
};

namespace gs {
template <typename Stream>
void serialize(Stream& stream, Vector3& value) {
    gs::read_or_write_bytes(stream, value);
}

template <typename Stream>
void serialize(Stream& stream, A& value) {
    gs::serializer(stream, value.x, value.y, value.z, value.vec3);
}
}  // namespace gs

// After the overloads, which serializer.h's unqualified call must see.
#include <gs/serializer.h>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // Among the probe's own build outputs: `cabin run` keeps the caller's
    // working directory.
    const char* const dir = std::getenv("CABIN_PROFILE_DIR");
    check(dir != nullptr, "CABIN_PROFILE_DIR");
    if (dir == nullptr)
        return 1;
    const std::string path = std::string(dir) + "/port-probe-generic-serialization.bin";

    A written;
    written.x = 5;
    written.y = 7.5f;
    written.z[0] = 'a';
    written.z[1] = 'b';
    written.z[2] = '\0';
    written.vec3 = {1, 2, 3};
    {
        std::ofstream f(path, std::ofstream::binary);
        gs::serializer(f, written);
        check(f.good(), "write");
    }

    // The members follow one another in order, unpadded: 4 + 4 + 3 + 12 bytes.
    char bytes[32] = {};
    std::size_t size = 0;
    {
        std::ifstream f(path, std::ios::binary);
        f.read(bytes, sizeof bytes);
        size = static_cast<std::size_t>(f.gcount());
    }
    check(size == 23, "file size");
    int x = 0;
    float y = 0, vec3[3] = {0, 0, 0};
    std::memcpy(&x, bytes, sizeof x);
    std::memcpy(&y, bytes + 4, sizeof y);
    std::memcpy(vec3, bytes + 11, sizeof vec3);
    check(x == 5 && y == 7.5f, "x and y bytes");
    check(std::memcmp(bytes + 8, "ab", 3) == 0, "z bytes");
    check(vec3[0] == 1 && vec3[1] == 2 && vec3[2] == 3, "vec3 bytes");

    A read = {};
    read.z[2] = 'x';
    {
        std::ifstream f(path, std::ios::binary);
        gs::serializer(f, read);
        check(f.good(), "read");
    }
    check(read.x == 5, "x");
    check(read.y == 7.5f, "y");
    check(read.z[0] == 'a' && read.z[1] == 'b' && read.z[2] == '\0', "z");
    check(read.vec3.x == 1 && read.vec3.y == 2 && read.vec3.z == 3, "vec3");

    check(std::remove(path.c_str()) == 0, "remove");
    if (failures != 0)
        return 1;
    std::puts("generic_serialization consumer test passed");
    return 0;
}
