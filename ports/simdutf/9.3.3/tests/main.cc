// The C++ consumer, through simdutf.h at C++17: every kernel compiled in
// that this CPU can run, the run-time-selected one among them, transcodes
// UTF-8, UTF-16LE, UTF-32 and Latin-1, counts, finds UTF-8 errors and
// codes base64 over inputs long enough for its vector loops, with known
// results and the same base64 text as the others; and the free functions,
// which dispatch to the selected kernel.
#include <simdutf.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string &what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// "héllo, 世界 😀": 19 bytes, 11 code points, 12 UTF-16 code units.
const std::string sample = "h\xc3\xa9llo, \xe4\xb8\x96\xe7\x95\x8c \xf0\x9f\x98\x80";
constexpr std::size_t repeat = 300;

std::string base64_reference;

void exercise(const simdutf::implementation &impl)
{
    const std::string what = std::string(impl.name()) + ": ";
    std::string text;
    for (std::size_t i = 0; i < repeat; ++i) text += sample;
    std::string back(text.size(), '\0');

    check(impl.validate_utf8(text.data(), text.size()), what + "valid UTF-8");
    check(impl.count_utf8(text.data(), text.size()) == 11 * repeat, what + "code points");
    check(impl.utf16_length_from_utf8(text.data(), text.size()) == 12 * repeat,
          what + "UTF-16 length");

    std::vector<char16_t> u16(12 * repeat);
    unsigned char unit[2];
    check(impl.convert_utf8_to_utf16le(text.data(), text.size(), u16.data()) == u16.size(),
          what + "UTF-8 to UTF-16LE");
    std::memcpy(unit, &u16[12 * 250 + 10], 2);
    check(unit[0] == 0x3d && unit[1] == 0xd8, what + "little-endian high surrogate");
    check(impl.validate_utf16le(u16.data(), u16.size()) &&
              impl.utf8_length_from_utf16le(u16.data(), u16.size()) == text.size() &&
              impl.convert_utf16le_to_utf8(u16.data(), u16.size(), &back[0]) == text.size() &&
              back == text,
          what + "UTF-16LE back to UTF-8");

    std::vector<char32_t> u32(11 * repeat);
    back.assign(text.size(), '\0');
    check(impl.convert_utf8_to_utf32(text.data(), text.size(), u32.data()) == u32.size() &&
              u32[11 * 123 + 1] == 0xe9 && u32[11 * 123 + 10] == 0x1f600 &&
              impl.convert_utf32_to_utf8(u32.data(), u32.size(), &back[0]) == text.size() &&
              back == text,
          what + "UTF-32 round trip");

    // Truncate a two-byte é and a four-byte emoji, near the start, in the
    // middle and near the end: the error is reported at the lead byte.
    const std::size_t leads[] = {1, 19 * 100 + 1, 19 * 299 + 15};
    for (std::size_t lead : leads) {
        std::string bad = text;
        bad[lead + 1] = 'A';
        const simdutf::result r = impl.validate_utf8_with_errors(bad.data(), bad.size());
        check(!impl.validate_utf8(bad.data(), bad.size()) &&
                  r.error == simdutf::error_code::TOO_SHORT && r.count == lead,
              what + "UTF-8 error at " + std::to_string(lead));
    }

    std::string latin(256, '\0'), utf8(384, '\0');
    for (int i = 0; i < 256; ++i) latin[i] = static_cast<char>(i);
    check(impl.convert_latin1_to_utf8(latin.data(), latin.size(), &utf8[0]) == 384 &&
              utf8.compare(128 + 2 * (0xe9 - 0x80), 2, "\xc3\xa9") == 0 &&
              utf8.compare(382, 2, "\xc3\xbf") == 0,
          what + "Latin-1 to UTF-8");

    std::vector<char> bytes(3000), out(3000);
    std::uint32_t seed = 99;
    for (char &b : bytes) {
        seed = seed * 1103515245u + 12345u;
        b = static_cast<char>(seed >> 16);
    }
    std::string coded(simdutf::base64_length_from_binary(bytes.size()), '\0');
    check(coded.size() == 4000 &&
              impl.binary_to_base64(bytes.data(), bytes.size(), &coded[0]) == coded.size(),
          what + "base64 encoding");
    if (base64_reference.empty()) base64_reference = coded;
    check(coded == base64_reference, what + "same base64 as the other kernels");
    const simdutf::result r = impl.base64_to_binary(coded.data(), coded.size(), out.data());
    check(r.error == simdutf::error_code::SUCCESS && r.count == bytes.size() && out == bytes,
          what + "base64 decoding");
    coded[1234] = '*';
    const simdutf::result bad = impl.base64_to_binary(coded.data(), coded.size(), out.data());
    check(bad.error == simdutf::error_code::INVALID_BASE64_CHARACTER && bad.count == 1234,
          what + "invalid base64 character");
}

} // namespace

int main()
{
    check(std::strcmp(SIMDUTF_VERSION, "9.3.3") == 0, "version");

    const simdutf::implementation *active = simdutf::get_active_implementation();
    check(active != nullptr && active->supported_by_runtime_system(), "active kernel");
    std::size_t kernels = 0;
    bool active_listed = false;
    for (const simdutf::implementation *impl : simdutf::get_available_implementations()) {
        if (!impl->supported_by_runtime_system()) continue;
        ++kernels;
        active_listed = active_listed || impl->name() == active->name();
        exercise(*impl);
    }
    check(kernels > 0 && active_listed, "the active kernel is one this CPU runs");

    std::u16string u16(simdutf::utf16_length_from_utf8(sample.data(), sample.size()), u'\0');
    check(simdutf::convert_utf8_to_utf16(sample.data(), sample.size(), &u16[0]) == 12 &&
              u16 == u"héllo, 世界 \U0001F600",
          "free functions, host-order UTF-16");

    if (failures != 0) return 1;
    std::printf("simdutf C++ consumer ok (%u kernels, active %s)\n",
                static_cast<unsigned>(kernels), std::string(active->name()).c_str());
    return 0;
}
