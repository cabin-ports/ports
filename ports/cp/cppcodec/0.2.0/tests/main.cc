// cppcodec is C++-only.  On Windows, <windows.h> comes first and without
// NOMINMAX, so its min and max macros are live when the headers compile.
#ifdef _WIN32
#include <windows.h>
#endif

#include <cppcodec/base32_crockford.hpp>
#include <cppcodec/base32_hex.hpp>
#include <cppcodec/base32_rfc4648.hpp>
#include <cppcodec/base64_rfc4648.hpp>
#include <cppcodec/base64_url.hpp>
#include <cppcodec/base64_url_unpadded.hpp>
#include <cppcodec/hex_lower.hpp>
#include <cppcodec/hex_upper.hpp>
#include <cppcodec/parse_error.hpp>
// One of each family's global aliases (base64, base32, hex); the others
// declare the same names.
#include <cppcodec/base32_default_crockford.hpp>
#include <cppcodec/base64_default_url.hpp>
#include <cppcodec/hex_default_upper.hpp>

#include <stdint.h>
#include <stdio.h>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      ++failures;                                                         \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,    \
              #cond);                                                     \
    }                                                                     \
  } while (0)

typedef std::vector<uint8_t> bytes;

static bytes of(const std::string& s) { return bytes(s.begin(), s.end()); }

// RFC 4648's definition, bit by bit: read the input as one big-endian bit
// string, emit a symbol per `bits` bits, zero-extend the last group on the
// right, then pad with '=' to a multiple of `block` symbols (0: no padding).
static std::string model(const bytes& in, const char* alphabet, int bits,
                         size_t block) {
  std::string out;
  unsigned acc = 0;
  int held = 0;
  for (size_t i = 0; i < in.size(); ++i) {
    acc = (acc << 8) | in[i];
    held += 8;
    while (held >= bits) {
      held -= bits;
      out += alphabet[(acc >> held) & ((1u << bits) - 1)];
    }
    acc &= (1u << held) - 1;
  }
  if (held > 0) out += alphabet[(acc << (bits - held)) & ((1u << bits) - 1)];
  if (block != 0)
    while (out.size() % block != 0) out += '=';
  return out;
}

static std::vector<bytes> samples() {
  std::vector<bytes> all;
  uint32_t x = 12345;
  for (size_t n = 0; n <= 40; ++n) {
    bytes b(n);
    for (size_t i = 0; i < n; ++i) {
      x = x * 1664525u + 1013904223u;
      b[i] = static_cast<uint8_t>(x >> 24);
    }
    all.push_back(b);
  }
  bytes every(256);  // every byte value, so every symbol appears
  for (size_t i = 0; i < every.size(); ++i)
    every[i] = static_cast<uint8_t>(i);
  all.push_back(every);
  return all;
}

// The README's encode and decode forms (README.md:158-224), each with
// pointer-and-size and container input, against the model.
template <typename Codec>
static void check_codec(const char* alphabet, int bits, size_t block) {
  const std::vector<bytes> inputs = samples();
  for (size_t k = 0; k < inputs.size(); ++k) {
    const bytes& in = inputs[k];
    const std::string text(in.begin(), in.end());
    const std::string expect = model(in, alphabet, bits, block);
    const std::vector<char> expect_chars(expect.begin(), expect.end());

    CHECK(Codec::encode(in.data(), in.size()) == expect);
    CHECK(Codec::encode(text.data(), text.size()) == expect);
    CHECK(Codec::encode(in) == expect);
    CHECK(Codec::encode(text) == expect);
    CHECK(Codec::template encode<std::vector<char> >(in) == expect_chars);
    CHECK(Codec::template encode<std::vector<char> >(in.data(), in.size()) ==
          expect_chars);
    std::string reused("stale contents");
    Codec::encode(reused, in);
    CHECK(reused == expect);
    reused = "stale contents";
    Codec::encode(reused, text.data(), text.size());
    CHECK(reused == expect);
    CHECK(Codec::encoded_size(in.size()) == expect.size());

    // A buffer one longer than the encoding also gets a terminating '\0'.
    std::vector<char> buf(expect.size() + 1, '#');
    CHECK(Codec::encode(&buf[0], buf.size(), in) == expect.size());
    CHECK(std::string(buf.begin(), buf.end() - 1) == expect);
    CHECK(buf.back() == '\0');
    buf.assign(expect.size(), '#');  // exactly the size: no terminator
    CHECK(Codec::encode(buf.data(), buf.size(), in.data(), in.size()) ==
          expect.size());
    CHECK(buf == expect_chars);

    CHECK(Codec::decode(expect) == in);
    CHECK(Codec::decode(expect.data(), expect.size()) == in);
    CHECK(Codec::template decode<std::string>(expect) == text);
    CHECK(Codec::template decode<std::string>(expect.data(), expect.size()) ==
          text);
    bytes reused_bytes(7, 0xAA);
    Codec::decode(reused_bytes, expect);
    CHECK(reused_bytes == in);
    reused_bytes.assign(7, 0xAA);
    Codec::decode(reused_bytes, expect.data(), expect.size());
    CHECK(reused_bytes == in);

    // Exact without padding; with it, whole blocks of bits * block / 8 bytes.
    const size_t max_size = Codec::decoded_max_size(expect.size());
    CHECK(max_size == (block != 0 ? expect.size() / block * (bits * block / 8)
                                  : in.size()));
    bytes raw(max_size + 1, 0xEE);
    CHECK(Codec::decode(&raw[0], raw.size(), expect) == in.size());
    raw.resize(in.size());
    CHECK(raw == in);
    std::vector<char> raw_chars(max_size + 1, '#');
    CHECK(Codec::decode(&raw_chars[0], raw_chars.size(), expect.data(),
                        expect.size()) == in.size());
    CHECK(std::string(raw_chars.data(), text.size()) == text);
  }
}

struct rfc_vector {
  const char* plain;
  const char* base64;
  const char* base32;
  const char* base32_hex;
};

// RFC 4648, section 10.
static const rfc_vector rfc_vectors[] = {
    {"", "", "", ""},
    {"f", "Zg==", "MY======", "CO======"},
    {"fo", "Zm8=", "MZXQ====", "CPNG===="},
    {"foo", "Zm9v", "MZXW6===", "CPNMU==="},
    {"foob", "Zm9vYg==", "MZXW6YQ=", "CPNMUOG="},
    {"fooba", "Zm9vYmE=", "MZXW6YTB", "CPNMUOJ1"},
    {"foobar", "Zm9vYmFy", "MZXW6YTBOI======", "CPNMUOJ1E8======"},
};

// Runs decode(input) and reports which error it threw.
enum outcome { decoded, symbol, padding, length, other };

template <typename Codec>
static outcome decode_outcome(const std::string& input,
                              char* bad = nullptr) {
  try {
    Codec::decode(input);
    return decoded;
  } catch (const cppcodec::symbol_error& e) {
    if (bad) *bad = e.symbol();
    return symbol;
  } catch (const cppcodec::padding_error&) {
    return padding;
  } catch (const cppcodec::invalid_input_length&) {
    return length;
  } catch (const cppcodec::parse_error&) {
    return other;
  }
}

int main() {
  using namespace cppcodec;

  for (size_t i = 0; i < sizeof rfc_vectors / sizeof rfc_vectors[0]; ++i) {
    const rfc_vector& v = rfc_vectors[i];
    const std::string plain(v.plain);
    CHECK(base64_rfc4648::encode(plain) == v.base64);
    CHECK(base32_rfc4648::encode(plain) == v.base32);
    CHECK(base32_hex::encode(plain) == v.base32_hex);
    CHECK(base64_rfc4648::decode(std::string(v.base64)) == of(plain));
    CHECK(base32_rfc4648::decode(std::string(v.base32)) == of(plain));
    CHECK(base32_hex::decode(std::string(v.base32_hex)) == of(plain));
  }
  CHECK(hex_upper::encode(std::string("foobar")) == "666F6F626172");
  CHECK(hex_lower::encode(std::string("foobar")) == "666f6f626172");

  const char* upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const std::string b64 =
      std::string(upper) + "abcdefghijklmnopqrstuvwxyz0123456789";
  check_codec<base64_rfc4648>((b64 + "+/").c_str(), 6, 4);
  check_codec<base64_url>((b64 + "-_").c_str(), 6, 4);
  check_codec<base64_url_unpadded>((b64 + "-_").c_str(), 6, 0);
  check_codec<base32_rfc4648>("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", 5, 8);
  check_codec<base32_hex>("0123456789ABCDEFGHIJKLMNOPQRSTUV", 5, 8);
  // Crockford's alphabet, unpadded, zero-extended on the right.
  check_codec<base32_crockford>("0123456789ABCDEFGHJKMNPQRSTVWXYZ", 5, 0);
  check_codec<hex_upper>("0123456789ABCDEF", 4, 0);
  check_codec<hex_lower>("0123456789abcdef", 4, 0);

  // Alphabets that differ only in symbols 62 and 63.
  const bytes high = {0xFB, 0xFF};
  CHECK(base64_rfc4648::encode(high) == "+/8=");
  CHECK(base64_url::encode(high) == "-_8=");
  CHECK(base64_url_unpadded::encode(high) == "-_8");

  // The default headers' aliases name the codecs they say: "-_8=" tells
  // base64_url from base64_rfc4648 and base64_url_unpadded.
  CHECK(base64::encode(high) == "-_8=");
  CHECK(base32::encode(high) == base32_crockford::encode(high));
  CHECK(base32::encode(high) != base32_rfc4648::encode(high));
  CHECK(hex::encode(high) == "FBFF");

  // Case-insensitive decoding where the variants allow it; Crockford also
  // reads O as 0, I and L as 1, and skips hyphens.
  const std::vector<bytes> inputs = samples();
  for (size_t k = 0; k < inputs.size(); ++k) {
    const bytes& in = inputs[k];
    std::string s = base32_rfc4648::encode(in);
    for (size_t i = 0; i < s.size(); ++i)
      if (s[i] >= 'A' && s[i] <= 'Z') s[i] = static_cast<char>(s[i] + 32);
    CHECK(base32_rfc4648::decode(s) == in);
    s = base32_hex::encode(in);
    for (size_t i = 0; i < s.size(); ++i)
      if (s[i] >= 'A' && s[i] <= 'V') s[i] = static_cast<char>(s[i] + 32);
    CHECK(base32_hex::decode(s) == in);
    CHECK(hex_upper::decode(hex_lower::encode(in)) == in);
    CHECK(hex_lower::decode(hex_upper::encode(in)) == in);

    const std::string crock = base32_crockford::encode(in);
    std::string sloppy;
    const char* ones = "IiLl";
    for (size_t i = 0; i < crock.size(); ++i) {
      if (i % 4 == 0) sloppy += '-';
      char c = crock[i];
      if (c == '0') c = (i % 2) ? 'O' : 'o';
      else if (c == '1') c = ones[i % 4];
      else if (c >= 'A' && c <= 'Z' && i % 3 == 0) c = static_cast<char>(c + 32);
      sloppy += c;
    }
    CHECK(base32_crockford::decode(sloppy) == in);
  }

  // Errors: every one derives from parse_error.
  char bad = 0;
  CHECK(decode_outcome<base64_rfc4648>("Zm9v!mFy", &bad) == symbol);
  CHECK(bad == '!');
  CHECK(decode_outcome<base64_rfc4648>("-_8=", &bad) == symbol && bad == '-');
  CHECK(decode_outcome<base64_url>("+/8=", &bad) == symbol && bad == '+');
  CHECK(decode_outcome<base32_rfc4648>("M1======", &bad) == symbol &&
        bad == '1');
  CHECK(decode_outcome<base32_crockford>("C5U", &bad) == symbol && bad == 'U');
  CHECK(decode_outcome<hex_lower>("0g", &bad) == symbol && bad == 'g');
  // Padding is required by the padded variants and optional for
  // base64_url_unpadded, which still rejects it where it cannot be.
  CHECK(decode_outcome<base64_rfc4648>("Zg") == padding);
  CHECK(decode_outcome<base64_rfc4648>("Zg=") == padding);
  CHECK(decode_outcome<base64_rfc4648>("Zg=x") == padding);
  CHECK(decode_outcome<base64_rfc4648>("=Zg=") == padding);
  CHECK(decode_outcome<base64_rfc4648>("Zm9v====") == padding);
  CHECK(decode_outcome<base64_url>("Zg") == padding);
  CHECK(decode_outcome<base32_rfc4648>("MY") == padding);
  CHECK(base64_url_unpadded::decode(std::string("Zg")) == of("f"));
  CHECK(base64_url_unpadded::decode(std::string("Zg==")) == of("f"));
  CHECK(decode_outcome<base64_url_unpadded>("Zg=") == padding);
  CHECK(decode_outcome<base64_url_unpadded>("=Zg") == padding);
  // A tail too short to hold a byte.
  CHECK(decode_outcome<base64_rfc4648>("Z===") == length);
  CHECK(decode_outcome<base32_rfc4648>("MZX=====") == length);
  CHECK(decode_outcome<base32_crockford>("C5Q") == length);
  CHECK(decode_outcome<hex_lower>("abc") == length);

  try {
    base64_rfc4648::decode(std::string("@@@@"));
    CHECK(false);
  } catch (const std::domain_error& e) {
    CHECK(std::string(e.what()).find("parse error") == 0);
  }

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("cppcodec: ok");
  return 0;
}
