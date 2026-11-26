#include <winmd_reader.h>

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace winmd::reader;

static void check(bool ok, const char* what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

// Throws std::invalid_argument with exactly `message`.
template <typename F>
static void check_invalid(F&& f, const char* message)
{
    try
    {
        f();
    }
    catch (std::invalid_argument const& e)
    {
        check(std::string(e.what()) == message, message);
        return;
    }
    check(false, message);
}

int main()
{
    // ECMA-335 II.23.2 compressed unsigned integers (signature.h:4).
    struct { std::vector<uint8_t> bytes; uint32_t value; } const cases[] = {
        { { 0x03 }, 0x03 },
        { { 0x7F }, 0x7F },
        { { 0x80, 0x80 }, 0x80 },
        { { 0xAE, 0x57 }, 0x2E57 },
        { { 0xBF, 0xFF }, 0x3FFF },
        { { 0xC0, 0x00, 0x40, 0x00 }, 0x4000 },
        { { 0xDF, 0xFF, 0xFF, 0xFF }, 0x1FFFFFFF },
    };
    for (auto const& c : cases)
    {
        byte_view cursor{ c.bytes.data(), c.bytes.data() + c.bytes.size() };
        check(uncompress_unsigned(cursor) == c.value, "compressed value");
        check(cursor.size() == 0, "cursor consumed");
    }

    // signature.h:30
    std::vector<uint8_t> const bad{ 0xE0 };
    check_invalid([&] {
        byte_view cursor{ bad.data(), bad.data() + bad.size() };
        uncompress_unsigned(cursor);
    }, "Invalid compressed integer in blob");

    // database.h:235
    check_invalid([] { database db{ std::vector<uint8_t>(256, 0) }; },
                  "Invalid DOS signature");

    // database.h:37-40: a file shorter than the DOS header is not a database.
    auto const path = std::filesystem::temp_directory_path() /
        ("winmd-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    {
        std::ofstream out(path, std::ios::binary);
        out << "not a pe file...";
    }
    bool const is_db = database::is_database(path.u8string());
    std::filesystem::remove(path);
    check(!is_db, "short file is not a database");

    std::puts("ok");
}
