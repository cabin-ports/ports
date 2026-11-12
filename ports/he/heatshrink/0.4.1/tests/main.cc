// Uses heatshrink from C++, wrapping its headers in extern "C" as they have
// none: upstream's "abcdabcd" test vector, then a round trip of a string
// through std::vector buffers.
extern "C" {
#include <heatshrink_decoder.h>
#include <heatshrink_encoder.h>
}

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static std::vector<uint8_t> encode(const std::string& text, uint8_t window_sz2,
                                   uint8_t lookahead_sz2) {
  std::vector<uint8_t> in(text.begin(), text.end());
  std::vector<uint8_t> out;
  heatshrink_encoder* hse =
      heatshrink_encoder_alloc(window_sz2, lookahead_sz2);
  check(hse != nullptr, "encoder_alloc");
  if (hse == nullptr) {
    return out;
  }
  auto drain = [&]() {
    HSE_poll_res pres;
    do {
      uint8_t chunk[8];
      size_t count = 0;
      pres = heatshrink_encoder_poll(hse, chunk, sizeof chunk, &count);
      out.insert(out.end(), chunk, chunk + count);
    } while (pres == HSER_POLL_MORE);
  };
  size_t sunk = 0;
  while (sunk < in.size()) {
    size_t count = 0;
    if (heatshrink_encoder_sink(hse, &in[sunk], in.size() - sunk, &count) !=
        HSER_SINK_OK) {
      check(false, "encoder_sink");
      break;
    }
    sunk += count;
    drain();
  }
  // finish reports MORE until the last partial byte is out, which a poll
  // whose buffer filled first leaves pending.
  HSE_finish_res fres;
  while ((fres = heatshrink_encoder_finish(hse)) == HSER_FINISH_MORE) {
    drain();
  }
  check(fres == HSER_FINISH_DONE, "encoder done");
  heatshrink_encoder_free(hse);
  return out;
}

static std::string decode(std::vector<uint8_t> in, uint8_t window_sz2,
                          uint8_t lookahead_sz2) {
  std::string out;
  heatshrink_decoder* hsd =
      heatshrink_decoder_alloc(32, window_sz2, lookahead_sz2);
  check(hsd != nullptr, "decoder_alloc");
  if (hsd == nullptr) {
    return out;
  }
  size_t sunk = 0;
  while (sunk < in.size()) {
    size_t count = 0;
    if (heatshrink_decoder_sink(hsd, &in[sunk], in.size() - sunk, &count) !=
        HSDR_SINK_OK) {
      check(false, "decoder_sink");
      break;
    }
    sunk += count;
    HSD_poll_res pres;
    do {
      uint8_t chunk[8];
      pres = heatshrink_decoder_poll(hsd, chunk, sizeof chunk, &count);
      out.append(chunk, chunk + count);
    } while (pres == HSDR_POLL_MORE);
  }
  check(heatshrink_decoder_finish(hsd) == HSDR_FINISH_DONE, "decoder done");
  heatshrink_decoder_free(hsd);
  return out;
}

int main() {
  static const uint8_t abcdabcd[] = {0xb0, 0xd8, 0xac, 0x76, 0x40, 0x1b};
  check(encode("abcdabcd", 8, 3) ==
            std::vector<uint8_t>(abcdabcd, abcdabcd + sizeof abcdabcd),
        "encode abcdabcd");

  std::string text;
  for (int i = 0; i < 40; ++i) {
    text += "C++ round trip " + std::to_string(i % 7) + "; ";
  }
  std::vector<uint8_t> packed = encode(text, 8, 4);
  check(!packed.empty() && packed.size() < text.size() / 2, "text compresses");
  check(decode(packed, 8, 4) == text, "round trip");

  if (failures == 0) {
    std::puts("heatshrink: ok");
  }
  return failures == 0 ? 0 : 1;
}
