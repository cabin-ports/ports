#include <stdio.h>
#include <string.h>

#include <charls/charls.h>

enum { width = 64, height = 64, pixels = width * height };

static int ok = 1;

static int check(int cond, const char *what) {
    if (!cond) {
        printf("FAIL: %s\n", what);
        ok = 0;
    }
    return cond;
}

static unsigned char source[pixels];
static unsigned char encoded[pixels * 2];
static unsigned char decoded[pixels];

int main(void) {
    charls_frame_info frame = {width, height, 8, 1};
    charls_frame_info out;
    charls_jpegls_encoder *enc;
    charls_jpegls_decoder *dec;
    size_t estimated = 0, written = 0, i;
    int x, y;

    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            source[y * width + x] = (unsigned char) (x * 3 + y * 2 + ((x * y) % 5));

    check(strcmp(charls_get_version_string(), "2.4.5") == 0, "version string");

    enc = charls_jpegls_encoder_create();
    if (!check(enc != NULL, "encoder create")) return 1;
    check(charls_jpegls_encoder_set_frame_info(enc, &frame) == CHARLS_JPEGLS_ERRC_SUCCESS, "set frame info");
    check(charls_jpegls_encoder_get_estimated_destination_size(enc, &estimated) == CHARLS_JPEGLS_ERRC_SUCCESS
              && estimated <= sizeof encoded,
          "estimated size");
    check(charls_jpegls_encoder_set_destination_buffer(enc, encoded, sizeof encoded) == CHARLS_JPEGLS_ERRC_SUCCESS,
          "set destination");
    check(charls_jpegls_encoder_encode_from_buffer(enc, source, sizeof source, 0) == CHARLS_JPEGLS_ERRC_SUCCESS,
          "encode");
    check(charls_jpegls_encoder_get_bytes_written(enc, &written) == CHARLS_JPEGLS_ERRC_SUCCESS && written > 0
              && written < sizeof source,
          "bytes written");
    charls_jpegls_encoder_destroy(enc);

    dec = charls_jpegls_decoder_create();
    if (!check(dec != NULL, "decoder create")) return 1;
    check(charls_jpegls_decoder_set_source_buffer(dec, encoded, written) == CHARLS_JPEGLS_ERRC_SUCCESS, "set source");
    check(charls_jpegls_decoder_read_header(dec) == CHARLS_JPEGLS_ERRC_SUCCESS, "read header");
    memset(&out, 0, sizeof out);
    check(charls_jpegls_decoder_get_frame_info(dec, &out) == CHARLS_JPEGLS_ERRC_SUCCESS, "get frame info");
    check(out.width == width && out.height == height && out.bits_per_sample == 8 && out.component_count == 1,
          "frame info");
    check(charls_jpegls_decoder_decode_to_buffer(dec, decoded, sizeof decoded, 0) == CHARLS_JPEGLS_ERRC_SUCCESS,
          "decode");
    for (i = 0; i < sizeof source; ++i)
        if (decoded[i] != source[i]) {
            check(0, "lossless round trip");
            break;
        }
    charls_jpegls_decoder_destroy(dec);
    return ok ? 0 : 1;
}
