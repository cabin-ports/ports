// Drives libb2 from C++11 through its extern "C" header: a BLAKE2b and a
// BLAKE2s parameter block with salt, personalization, and a shorter digest,
// checked against Python's hashlib, and BLAKE2bp and BLAKE2sp fed byte by
// byte against their one-shot digests over input spanning several of the
// parallel leaves' blocks.
#include <blake2.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "libb2 C++ test failed: %s\n", what);
        ++failures;
    }
}

const std::string fox = "The quick brown fox jumps over the lazy dog";

// hashlib.blake2b(fox, digest_size=32, salt=b"cabin-ports salt",
//                 person=b"libb2 0.98.1 cxx")
const std::array<uint8_t, 32> b2b_salted = {{
    0xc6, 0xf7, 0xd3, 0x35, 0xce, 0xbd, 0x0f, 0x60, 0x12, 0x79, 0x25, 0x42, 0xaa, 0x07, 0x1c, 0x49,
    0x81, 0x3f, 0x6d, 0x6e, 0x50, 0x2f, 0xc3, 0x3d, 0xa1, 0x46, 0x8f, 0xa2, 0x75, 0x67, 0x84, 0xa1}};
// hashlib.blake2s(fox, digest_size=16, salt=b"saltsalt", person=b"cxx-test")
const std::array<uint8_t, 16> b2s_salted = {{
    0xfa, 0xc9, 0xbb, 0x8c, 0x9d, 0x8d, 0xa2, 0xac, 0xea, 0xd3, 0xb8, 0xa6, 0xde, 0x4d, 0x50, 0xb9}};

void test_param_blocks() {
    const uint8_t* in = reinterpret_cast<const uint8_t*>(fox.data());

    blake2b_param bp;
    std::memset(&bp, 0, sizeof bp);
    bp.digest_length = 32;
    bp.fanout = 1;
    bp.depth = 1;
    std::memcpy(bp.salt, "cabin-ports salt", BLAKE2B_SALTBYTES);
    std::memcpy(bp.personal, "libb2 0.98.1 cxx", BLAKE2B_PERSONALBYTES);
    blake2b_state b;
    std::array<uint8_t, 32> b_out{};
    check(blake2b_init_param(&b, &bp) == 0 && blake2b_update(&b, in, fox.size()) == 0 &&
              blake2b_final(&b, b_out.data(), b_out.size()) == 0 && b_out == b2b_salted,
          "salted, personalized BLAKE2b-256");

    blake2s_param sp;
    std::memset(&sp, 0, sizeof sp);
    sp.digest_length = 16;
    sp.fanout = 1;
    sp.depth = 1;
    std::memcpy(sp.salt, "saltsalt", BLAKE2S_SALTBYTES);
    std::memcpy(sp.personal, "cxx-test", BLAKE2S_PERSONALBYTES);
    blake2s_state s;
    std::array<uint8_t, 16> s_out{};
    check(blake2s_init_param(&s, &sp) == 0 && blake2s_update(&s, in, fox.size()) == 0 &&
              blake2s_final(&s, s_out.data(), s_out.size()) == 0 && s_out == b2s_salted,
          "salted, personalized BLAKE2s-128");
}

void test_parallel_streaming() {
    // 4 leaves x 128 bytes for BLAKE2bp and 8 x 64 for BLAKE2sp: 3000 bytes
    // gives each leaf several blocks and one leaf a partial last block.
    std::vector<uint8_t> data(3000);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(i * 7 + 3);

    std::array<uint8_t, BLAKE2B_OUTBYTES> b_once{}, b_streamed{};
    blake2bp_state b;
    check(blake2bp(b_once.data(), data.data(), nullptr, b_once.size(), data.size(), 0) == 0 &&
              blake2bp_init(&b, b_streamed.size()) == 0,
          "BLAKE2bp setup");
    for (uint8_t byte : data)
        check(blake2bp_update(&b, &byte, 1) == 0, "BLAKE2bp update");
    check(blake2bp_final(&b, b_streamed.data(), b_streamed.size()) == 0 && b_once == b_streamed,
          "BLAKE2bp byte-at-a-time matches one-shot");

    std::array<uint8_t, BLAKE2S_OUTBYTES> s_once{}, s_streamed{};
    blake2sp_state s;
    check(blake2sp(s_once.data(), data.data(), nullptr, s_once.size(), data.size(), 0) == 0 &&
              blake2sp_init(&s, s_streamed.size()) == 0,
          "BLAKE2sp setup");
    for (uint8_t byte : data)
        check(blake2sp_update(&s, &byte, 1) == 0, "BLAKE2sp update");
    check(blake2sp_final(&s, s_streamed.data(), s_streamed.size()) == 0 && s_once == s_streamed,
          "BLAKE2sp byte-at-a-time matches one-shot");

    std::array<uint8_t, BLAKE2B_OUTBYTES> plain{};
    check(blake2b(plain.data(), data.data(), nullptr, plain.size(), data.size(), 0) == 0 &&
              plain != b_once,
          "BLAKE2bp is not BLAKE2b");
}

}  // namespace

int main() {
    test_param_blocks();
    test_parallel_streaming();
    if (failures != 0)
        return 1;
    std::puts("libb2 C++ ok");
    return 0;
}
