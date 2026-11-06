#include <brotli/decode.h>
#include <brotli/encode.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int fail(const char* what) {
  std::fprintf(stderr, "brotli: %s\n", what);
  return 1;
}

// Streams `input` through an encoder in 1000-byte chunks.
bool compress(const std::string& input, int quality,
              const BrotliEncoderPreparedDictionary* dictionary,
              std::vector<uint8_t>& out) {
  BrotliEncoderState* enc = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
  if (!enc) return false;
  bool ok = BrotliEncoderSetParameter(enc, BROTLI_PARAM_QUALITY, quality) &&
            (!dictionary || BrotliEncoderAttachPreparedDictionary(enc, dictionary));
  const uint8_t* next_in = reinterpret_cast<const uint8_t*>(input.data());
  size_t remaining = input.size();
  while (ok && !BrotliEncoderIsFinished(enc)) {
    size_t chunk = remaining < 1000 ? remaining : 1000;
    size_t avail_in = chunk;
    BrotliEncoderOperation op =
        chunk == remaining ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS;
    uint8_t buffer[512];
    uint8_t* next_out = buffer;
    size_t avail_out = sizeof buffer;
    ok = BrotliEncoderCompressStream(enc, op, &avail_in, &next_in, &avail_out,
                                     &next_out, nullptr);
    remaining -= chunk - avail_in;
    out.insert(out.end(), buffer, next_out);
  }
  BrotliEncoderDestroyInstance(enc);
  return ok;
}

// Feeds `in` to a decoder 7 bytes at a time; returns the final result.
BrotliDecoderResult decompress(const std::vector<uint8_t>& in,
                               const std::string* dictionary,
                               std::string& out) {
  BrotliDecoderState* dec = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
  if (!dec) return BROTLI_DECODER_RESULT_ERROR;
  if (dictionary &&
      !BrotliDecoderAttachDictionary(
          dec, BROTLI_SHARED_DICTIONARY_RAW, dictionary->size(),
          reinterpret_cast<const uint8_t*>(dictionary->data()))) {
    BrotliDecoderDestroyInstance(dec);
    return BROTLI_DECODER_RESULT_ERROR;
  }
  const uint8_t* next_in = in.data();
  size_t fed = 0;
  size_t avail_in = 0;
  BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT;
  while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT && fed < in.size()) {
    size_t chunk = in.size() - fed < 7 ? in.size() - fed : 7;
    avail_in += chunk;
    fed += chunk;
    do {
      uint8_t buffer[4096];
      uint8_t* next_out = buffer;
      size_t avail_out = sizeof buffer;
      result = BrotliDecoderDecompressStream(dec, &avail_in, &next_in,
                                             &avail_out, &next_out, nullptr);
      out.append(reinterpret_cast<const char*>(buffer),
                 static_cast<size_t>(next_out - buffer));
    } while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);
  }
  if (result == BROTLI_DECODER_RESULT_SUCCESS &&
      (avail_in != 0 || fed != in.size() || !BrotliDecoderIsFinished(dec)))
    result = BROTLI_DECODER_RESULT_ERROR;
  BrotliDecoderDestroyInstance(dec);
  return result;
}

}  // namespace

int main() {
  std::string text;
  for (int i = 0; i < 3000; ++i)
    text += "line " + std::to_string(i % 97) + ": the quick brown fox\n";

  std::vector<uint8_t> stream;
  if (!compress(text, 5, nullptr, stream)) return fail("stream compress");
  if (stream.size() >= text.size() / 4) return fail("stream ratio");
  std::string round;
  if (decompress(stream, nullptr, round) != BROTLI_DECODER_RESULT_SUCCESS ||
      round != text)
    return fail("stream round trip");

  std::vector<uint8_t> truncated(stream.begin(), stream.end() - 1);
  std::string partial;
  if (decompress(truncated, nullptr, partial) !=
      BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT)
    return fail("truncated stream");

  // A raw prefix dictionary shared by both sides shrinks a message that
  // repeats it.
  const std::string dictionary =
      "{\"type\":\"telemetry\",\"device\":\"sensor-unit\",\"units\":\"celsius\","
      "\"firmware\":\"2026.11.0\",\"status\":\"nominal\"}";
  const std::string message = dictionary + dictionary;
  BrotliEncoderPreparedDictionary* prepared = BrotliEncoderPrepareDictionary(
      BROTLI_SHARED_DICTIONARY_RAW, dictionary.size(),
      reinterpret_cast<const uint8_t*>(dictionary.data()), BROTLI_MAX_QUALITY,
      nullptr, nullptr, nullptr);
  if (!prepared) return fail("prepare dictionary");
  std::vector<uint8_t> plain, with_dictionary;
  bool compressed = compress(message, BROTLI_MAX_QUALITY, nullptr, plain) &&
                    compress(message, BROTLI_MAX_QUALITY, prepared, with_dictionary);
  BrotliEncoderDestroyPreparedDictionary(prepared);
  if (!compressed) return fail("dictionary compress");
  if (with_dictionary.size() >= plain.size()) return fail("dictionary ratio");
  std::string restored;
  if (decompress(with_dictionary, &dictionary, restored) !=
          BROTLI_DECODER_RESULT_SUCCESS ||
      restored != message)
    return fail("dictionary round trip");
  return 0;
}
