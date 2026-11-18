#include <datraw.h>

#include <cstdio>
#include <stdexcept>

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    typedef datraw::info<char> info;

    bool threw = false;
    try {
        info::parse("");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    check(threw, "empty input throws runtime_error");

    // Upstream's test/info.cpp:41-67.
    const info i = info::parse(
        "ObjectFileName: foot.raw\n"
        "TaggedFileName: ---\n"
        "Resolution: 256 256 256\n"
        "SliceThickness: 1 1 1\n"
        "Format: UCHAR\n"
        "NbrTags: 0\n"
        "ObjectType: TEXTURE_VOLUME_OBJECT\n"
        "ObjectModel: RGBA\n"
        "GridType: EQUIDISTANT\n");
    check(i.object_file_name() == "foot.raw", "object file name");
    const auto resolution = i.resolution();
    check(resolution.size() == 3 && resolution[0] == 256 && resolution[1] == 256
              && resolution[2] == 256,
          "resolution");
    check(i.format() == datraw::scalar_type::uint8, "format");
    check(i.grid_type() == datraw::grid_type::cartesian, "grid type");
    check(i.dimensions() == 3, "dimensions");
    check(i.components() == 1, "components");
    check(i.time_steps() == 1, "time steps");
    check(i.byte_order() == datraw::endianness::little, "byte order");
    check(i.row_size() == 256, "row size");

    // Cartesian is also the default grid type, so parse a non-default one.
    const info rectilinear = info::parse(
        "ObjectFileName: foot.raw\n"
        "Resolution: 256 256 256\n"
        "Format: UCHAR\n"
        "GridType: RECTILINEAR\n");
    check(rectilinear.grid_type() == datraw::grid_type::rectilinear, "rectilinear grid type");

    if (failures != 0) {
        return 1;
    }
    std::puts("datraw consumer test passed");
    return 0;
}
