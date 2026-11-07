/* The C consumer: libFLAC's stream encoder and decoder through memory
 * callbacks, lossless round trips of 16-, 24- and 32-bit audio in native
 * FLAC and in Ogg FLAC, metadata blocks, sample-accurate seeking,
 * multithreaded encoding, files with a UTF-8 name, and damaged input. */
#include <FLAC/metadata.h>
#include <FLAC/stream_decoder.h>
#include <FLAC/stream_encoder.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#endif

static int ok = 1;

static void check(int pass, const char *what) {
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

enum { RATE = 44100, FRAMES = 44100, SILENCE = FRAMES / 2 };

/* A sawtooth per channel with pseudo-random noise, and a stretch of digital
 * silence, at the given sample width. */
static FLAC__int32 *make_signal(uint32_t channels, uint32_t bps) {
    FLAC__int32 *pcm = malloc(sizeof *pcm * FRAMES * channels);
    const FLAC__int64 peak = ((FLAC__int64)1 << (bps - 2)) - 1;
    FLAC__uint32 state = 12345u + bps;
    size_t i;
    uint32_t c;
    for (i = 0; pcm && i < FRAMES; i++) {
        for (c = 0; c < channels; c++) {
            const FLAC__int64 period = 100 + 37 * c;
            const FLAC__int64 ramp = (FLAC__int64)(i % period) * 2 * peak / period - peak;
            state = state * 1664525u + 1013904223u;
            pcm[i * channels + c] = i >= SILENCE && i < SILENCE + 4096
                                        ? 0
                                        : (FLAC__int32)(ramp / 2 + (FLAC__int64)(state >> 16) % 2001 - 1000);
        }
    }
    return pcm;
}

/* An in-memory file the encoder writes, seeks and reads back (Ogg). */
typedef struct {
    FLAC__byte *data;
    size_t size, capacity, pos;
} Memory;

static FLAC__StreamEncoderWriteStatus enc_write(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[],
                                                size_t bytes, uint32_t samples, uint32_t frame, void *client) {
    Memory *m = client;
    (void)encoder, (void)samples, (void)frame;
    if (m->pos + bytes > m->capacity) {
        size_t capacity = m->capacity ? m->capacity : 4096;
        FLAC__byte *data;
        while (capacity < m->pos + bytes) capacity *= 2;
        if (!(data = realloc(m->data, capacity))) return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
        m->data = data;
        m->capacity = capacity;
    }
    memcpy(m->data + m->pos, buffer, bytes);
    m->pos += bytes;
    if (m->pos > m->size) m->size = m->pos;
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static FLAC__StreamEncoderSeekStatus enc_seek(const FLAC__StreamEncoder *encoder, FLAC__uint64 offset, void *client) {
    Memory *m = client;
    (void)encoder;
    if (offset > m->size) return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
    m->pos = (size_t)offset;
    return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

static FLAC__StreamEncoderTellStatus enc_tell(const FLAC__StreamEncoder *encoder, FLAC__uint64 *offset, void *client) {
    (void)encoder;
    *offset = ((Memory *)client)->pos;
    return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

static FLAC__StreamEncoderReadStatus enc_read(const FLAC__StreamEncoder *encoder, FLAC__byte buffer[], size_t *bytes,
                                              void *client) {
    Memory *m = client;
    size_t n = m->size - m->pos;
    (void)encoder;
    if (n > *bytes) n = *bytes;
    memcpy(buffer, m->data + m->pos, n);
    m->pos += n;
    *bytes = n;
    return n ? FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE : FLAC__STREAM_ENCODER_READ_STATUS_END_OF_STREAM;
}

typedef struct {
    uint32_t channels, bps, level, threads;
    int ogg;
    FLAC__StreamMetadata **metadata;
    uint32_t blocks;
} Settings;

/* Encodes pcm into out, or into the file at path; returns the
 * set_num_threads status, or -1 if encoding failed. */
static int encode(Memory *out, const char *path, const FLAC__int32 *pcm, const Settings *s) {
    FLAC__StreamEncoder *encoder = FLAC__stream_encoder_new();
    FLAC__StreamEncoderInitStatus init;
    int threads = FLAC__STREAM_ENCODER_SET_NUM_THREADS_OK, good;
    size_t i;
    if (!encoder) return -1;
    good = FLAC__stream_encoder_set_channels(encoder, s->channels) &&
           FLAC__stream_encoder_set_bits_per_sample(encoder, s->bps) &&
           FLAC__stream_encoder_set_sample_rate(encoder, RATE) &&
           FLAC__stream_encoder_set_compression_level(encoder, s->level) &&
           FLAC__stream_encoder_set_streamable_subset(encoder, s->bps <= 24) &&
           FLAC__stream_encoder_set_total_samples_estimate(encoder, FRAMES) &&
           FLAC__stream_encoder_set_metadata(encoder, s->metadata, s->blocks);
    if (s->threads > 1) threads = (int)FLAC__stream_encoder_set_num_threads(encoder, s->threads);
    else good = good && FLAC__stream_encoder_set_verify(encoder, true);
    if (s->ogg) good = good && FLAC__stream_encoder_set_ogg_serial_number(encoder, 0x464c4143);
    if (path) init = FLAC__stream_encoder_init_file(encoder, path, NULL, NULL);
    else if (s->ogg) init = FLAC__stream_encoder_init_ogg_stream(encoder, enc_read, enc_write, enc_seek, enc_tell, NULL, out);
    else init = FLAC__stream_encoder_init_stream(encoder, enc_write, enc_seek, enc_tell, NULL, out);
    good = good && init == FLAC__STREAM_ENCODER_INIT_STATUS_OK;
    for (i = 0; good && i < FRAMES; i += 1000) {
        const uint32_t n = FRAMES - i < 1000 ? (uint32_t)(FRAMES - i) : 1000;
        good = FLAC__stream_encoder_process_interleaved(encoder, pcm + i * s->channels, n);
    }
    good = FLAC__stream_encoder_finish(encoder) && good;
    FLAC__stream_encoder_delete(encoder);
    return good ? threads : -1;
}

/* What a decoder reports, and the stream it reads. */
typedef struct {
    const Memory *in;
    size_t pos;
    FLAC__int32 *pcm;
    size_t frames;
    FLAC__uint64 first;
    int errors, have_first;
    FLAC__StreamMetadata *streaminfo, *tags, *seektable;
} Decoded;

static FLAC__StreamDecoderReadStatus dec_read(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes,
                                              void *client) {
    Decoded *d = client;
    size_t n = d->in->size - d->pos;
    (void)decoder;
    if (n > *bytes) n = *bytes;
    memcpy(buffer, d->in->data + d->pos, n);
    d->pos += n;
    *bytes = n;
    return n ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
}

static FLAC__StreamDecoderSeekStatus dec_seek(const FLAC__StreamDecoder *decoder, FLAC__uint64 offset, void *client) {
    Decoded *d = client;
    (void)decoder;
    if (offset > d->in->size) return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    d->pos = (size_t)offset;
    return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__StreamDecoderTellStatus dec_tell(const FLAC__StreamDecoder *decoder, FLAC__uint64 *offset, void *client) {
    (void)decoder;
    *offset = ((Decoded *)client)->pos;
    return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__StreamDecoderLengthStatus dec_length(const FLAC__StreamDecoder *decoder, FLAC__uint64 *length,
                                                  void *client) {
    (void)decoder;
    *length = ((Decoded *)client)->in->size;
    return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool dec_eof(const FLAC__StreamDecoder *decoder, void *client) {
    Decoded *d = client;
    (void)decoder;
    return d->pos >= d->in->size;
}

static FLAC__StreamDecoderWriteStatus dec_write(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame,
                                                const FLAC__int32 *const buffer[], void *client) {
    Decoded *d = client;
    const uint32_t channels = frame->header.channels, n = frame->header.blocksize;
    uint32_t i, c;
    (void)decoder;
    if (!d->have_first) {
        d->first = frame->header.number.sample_number;
        d->have_first = 1;
    }
    if (d->frames + n > FRAMES) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    for (i = 0; i < n; i++)
        for (c = 0; c < channels; c++) d->pcm[(d->frames + i) * channels + c] = buffer[c][i];
    d->frames += n;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void dec_metadata(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client) {
    Decoded *d = client;
    (void)decoder;
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) d->streaminfo = FLAC__metadata_object_clone(metadata);
    if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) d->tags = FLAC__metadata_object_clone(metadata);
    if (metadata->type == FLAC__METADATA_TYPE_SEEKTABLE) d->seektable = FLAC__metadata_object_clone(metadata);
}

static void dec_error(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client) {
    (void)decoder, (void)status;
    ((Decoded *)client)->errors++;
}

static void release(Decoded *d) {
    free(d->pcm);
    if (d->streaminfo) FLAC__metadata_object_delete(d->streaminfo);
    if (d->tags) FLAC__metadata_object_delete(d->tags);
    if (d->seektable) FLAC__metadata_object_delete(d->seektable);
}

/* Decodes from in, or from the file at path, with MD5 checking, from
 * sample `from` on; returns whether finishing verified the MD5 signature
 * (which seeking turns off). */
static int decode(Decoded *d, const Memory *in, const char *path, uint32_t channels, int ogg, FLAC__uint64 from) {
    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    FLAC__StreamDecoderInitStatus init;
    int good;
    memset(d, 0, sizeof *d);
    d->in = in;
    d->pcm = malloc(sizeof *d->pcm * FRAMES * channels);
    if (!decoder || !d->pcm) return 0;
    FLAC__stream_decoder_set_md5_checking(decoder, true);
    FLAC__stream_decoder_set_metadata_respond_all(decoder);
    if (path)
        init = FLAC__stream_decoder_init_file(decoder, path, dec_write, dec_metadata, dec_error, d);
    else if (ogg)
        init = FLAC__stream_decoder_init_ogg_stream(decoder, dec_read, dec_seek, dec_tell, dec_length, dec_eof, dec_write,
                                                    dec_metadata, dec_error, d);
    else
        init = FLAC__stream_decoder_init_stream(decoder, dec_read, dec_seek, dec_tell, dec_length, dec_eof, dec_write,
                                                dec_metadata, dec_error, d);
    good = init == FLAC__STREAM_DECODER_INIT_STATUS_OK;
    if (good && from) {
        good = FLAC__stream_decoder_process_until_end_of_metadata(decoder) &&
               FLAC__stream_decoder_seek_absolute(decoder, from);
    }
    good = good && FLAC__stream_decoder_process_until_end_of_stream(decoder) &&
           FLAC__stream_decoder_get_state(decoder) == FLAC__STREAM_DECODER_END_OF_STREAM;
    good = FLAC__stream_decoder_finish(decoder) && good;
    FLAC__stream_decoder_delete(decoder);
    return good;
}

static int same_samples(const FLAC__int32 *a, const FLAC__int32 *b, size_t count) {
    return memcmp(a, b, sizeof *a * count) == 0;
}

static void version(void) {
    check(strcmp(FLAC__VERSION_STRING, "1.5.0") == 0, "FLAC__VERSION_STRING");
    check(strncmp(FLAC__VENDOR_STRING, "reference libFLAC 1.5.0 ", 24) == 0, "FLAC__VENDOR_STRING");
    check(FLAC_API_SUPPORTS_OGG_FLAC == 1, "built with Ogg FLAC");
    check(FLAC__format_vorbiscomment_entry_name_is_legal("TITLE") &&
              !FLAC__format_vorbiscomment_entry_name_is_legal("TI=TLE"),
          "Vorbis comment field names");
}

/* Lossless round trip in memory at one width, layout and container.  `md5sum`
 * is the digest of the signal's little-endian samples, computed outside
 * libFLAC, so a wrong byte order in the encoder's MD5 fails even though its
 * own decoder would agree with it. */
static void round_trip(uint32_t channels, uint32_t bps, uint32_t level, int ogg, const char *md5sum) {
    char what[96], hex[33];
    int k;
    FLAC__int32 *pcm = make_signal(channels, bps);
    Memory out = {0};
    Decoded d;
    const Settings s = {channels, bps, level, 1, ogg, NULL, 0};
    const size_t raw = (size_t)FRAMES * channels * bps / 8;
    int md5;
    snprintf(what, sizeof what, "%u-bit, %u channel(s), level %u%s", bps, channels, level, ogg ? ", Ogg" : "");
    if (!pcm || encode(&out, NULL, pcm, &s) != 0) {
        fprintf(stderr, "FAIL: encoding %s\n", what);
        ok = 0;
        free(pcm);
        free(out.data);
        return;
    }
    md5 = decode(&d, &out, NULL, channels, ogg, 0);
    check(out.size > 4 && memcmp(out.data, ogg ? "OggS" : "fLaC", 4) == 0, what);
    check(out.size < raw * 9 / 10, what);
    check(md5 && d.errors == 0, what);
    check(d.frames == FRAMES && same_samples(d.pcm, pcm, (size_t)FRAMES * channels), what);
    check(d.streaminfo && d.streaminfo->data.stream_info.total_samples == FRAMES &&
              d.streaminfo->data.stream_info.channels == channels &&
              d.streaminfo->data.stream_info.bits_per_sample == bps &&
              d.streaminfo->data.stream_info.sample_rate == RATE,
          what);
    for (k = 0; d.streaminfo && k < 16; k++) snprintf(hex + 2 * k, 3, "%02x", d.streaminfo->data.stream_info.md5sum[k]);
    check(d.streaminfo && strcmp(hex, md5sum) == 0, "STREAMINFO carries the MD5 of the little-endian samples");
    release(&d);
    free(out.data);
    free(pcm);
}

static FLAC__StreamMetadata *make_tags(void) {
    FLAC__StreamMetadata *tags = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
    FLAC__StreamMetadata_VorbisComment_Entry entry;
    if (!tags) return NULL;
    if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, "TITLE", "Sawtooth") ||
        !FLAC__metadata_object_vorbiscomment_append_comment(tags, entry, false) ||
        !FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, "ARTIST", "Cabin") ||
        !FLAC__metadata_object_vorbiscomment_append_comment(tags, entry, false)) {
        FLAC__metadata_object_delete(tags);
        return NULL;
    }
    return tags;
}

static int has_title(const FLAC__StreamMetadata *tags) {
    const int at = tags ? FLAC__metadata_object_vorbiscomment_find_entry_from(tags, 0, "TITLE") : -1;
    return at >= 0 && strcmp((const char *)tags->data.vorbis_comment.comments[at].entry, "TITLE=Sawtooth") == 0;
}

/* A Vorbis comment, a seek table the encoder fills in, and padding; then
 * a seek to a sample in the middle of a block. */
static void metadata_and_seeking(void) {
    FLAC__int32 *pcm = make_signal(2, 16);
    FLAC__StreamMetadata *blocks[3];
    Memory out = {0};
    Decoded d;
    const Settings s = {2, 16, 5, 1, 0, blocks, 3};
    blocks[0] = make_tags();
    blocks[1] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_SEEKTABLE);
    blocks[2] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING);
    if (!pcm || !blocks[0] || !blocks[1] || !blocks[2] ||
        !FLAC__metadata_object_seektable_template_append_spaced_points_by_samples(blocks[1], 8192, FRAMES)) {
        check(0, "building metadata blocks");
    } else {
        blocks[2]->length = 1000;
        check(encode(&out, NULL, pcm, &s) == 0, "encoding with metadata");
        check(decode(&d, &out, NULL, 2, 0, 0), "decoding with metadata");
        check(has_title(d.tags) && d.tags->data.vorbis_comment.num_comments == 2, "the Vorbis comment survives");
        check(d.seektable && FLAC__format_seektable_is_legal(&d.seektable->data.seek_table) &&
                  d.seektable->data.seek_table.num_points == 6 &&
                  d.seektable->data.seek_table.points[1].sample_number == 8192 &&
                  d.seektable->data.seek_table.points[1].stream_offset > 0,
              "the encoder fills in the seek table");
        release(&d);

        check(decode(&d, &out, NULL, 2, 0, 30001), "seeking to sample 30001");
        check(d.have_first && d.first == 30001 && d.frames == FRAMES - 30001 &&
                  same_samples(d.pcm, pcm + 30001 * 2, (size_t)(FRAMES - 30001) * 2),
              "decoding resumes at the exact sample sought");
        release(&d);
    }
    for (int k = 0; k < 3; k++)
        if (blocks[k]) FLAC__metadata_object_delete(blocks[k]);
    free(out.data);
    free(pcm);
}

/* Several encoder threads, where libFLAC is built with them: off Windows,
 * and with GCC for MinGW-w64.  cl, clang-cl and Clang targeting MSVC have
 * no pthreads; Clang targeting MinGW may get either. */
static void multithreaded(void) {
    FLAC__int32 *pcm = make_signal(2, 16);
    Memory out = {0};
    Decoded d;
    const Settings s = {2, 16, 5, 4, 0, NULL, 0};
    const int status = pcm ? encode(&out, NULL, pcm, &s) : -1;
#if defined(_WIN32) && !(defined(__MINGW32__) && !defined(__clang__))
    check(status == FLAC__STREAM_ENCODER_SET_NUM_THREADS_OK ||
              status == FLAC__STREAM_ENCODER_SET_NUM_THREADS_NOT_COMPILED_WITH_MULTITHREADING_ENABLED,
          "encoding with four threads requested");
#else
    check(status == FLAC__STREAM_ENCODER_SET_NUM_THREADS_OK, "encoding on four threads");
#endif
    check(status >= 0 && decode(&d, &out, NULL, 2, 0, 0) && d.frames == FRAMES &&
              same_samples(d.pcm, pcm, (size_t)FRAMES * 2),
          "the multithreaded stream decodes losslessly");
    if (status >= 0) release(&d);
    free(out.data);
    free(pcm);
}

/* File I/O under a UTF-8 name, which libFLAC opens as UTF-8 on Windows
 * too, and the file-based metadata functions. */
static void files(void) {
    /* Among the probe's own build outputs. */
    char path[4096];
#ifdef _WIN32
    /* The environment's narrow strings are in the ANSI code page, not
     * UTF-8, so the name is made wide and converted. */
    const wchar_t *wdir = _wgetenv(L"CABIN_PROFILE_DIR");
    wchar_t wpath[4096];
    const int have = wdir && swprintf(wpath, 4096, L"%ls/port-probe-libflac-\x00fc.flac", wdir) > 0 &&
                     WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, sizeof path, NULL, NULL) > 0;
#else
    const char *dir = getenv("CABIN_PROFILE_DIR");
    const int have = dir && snprintf(path, sizeof path, "%s/port-probe-libflac-\xc3\xbc.flac", dir) < (int)sizeof path;
#endif
    FLAC__int32 *pcm = make_signal(1, 16);
    FLAC__StreamMetadata *blocks[1];
    FLAC__StreamMetadata info, *tags = NULL;
    Decoded d;
    const Settings s = {1, 16, 5, 1, 0, blocks, 1};
    if (!have || !pcm) {
        check(0, "CABIN_PROFILE_DIR");
        free(pcm);
        return;
    }
    blocks[0] = make_tags();
    check(blocks[0] && encode(NULL, path, pcm, &s) == 0, "encoding to a file");
    check(FLAC__metadata_get_streaminfo(path, &info) && info.data.stream_info.total_samples == FRAMES,
          "FLAC__metadata_get_streaminfo");
    check(FLAC__metadata_get_tags(path, &tags) && has_title(tags), "FLAC__metadata_get_tags");
    check(decode(&d, NULL, path, 1, 0, 0) && d.frames == FRAMES && same_samples(d.pcm, pcm, FRAMES),
          "decoding from a file");
    release(&d);
#ifdef _WIN32
    {
        FILE *f = _wfopen(wpath, L"rb");
        char magic[4] = {0};
        check(f && fread(magic, 1, 4, f) == 4 && memcmp(magic, "fLaC", 4) == 0, "the file name is UTF-8");
        if (f) fclose(f);
        _wremove(wpath);
    }
#else
    remove(path);
#endif
    if (tags) FLAC__metadata_object_delete(tags);
    if (blocks[0]) FLAC__metadata_object_delete(blocks[0]);
    free(pcm);
}

/* One damaged byte in the audio frames: the decoder reports it and goes
 * on. */
static void damaged(void) {
    FLAC__int32 *pcm = make_signal(2, 16);
    Memory out = {0};
    Decoded d;
    const Settings s = {2, 16, 5, 1, 0, NULL, 0};
    if (pcm && encode(&out, NULL, pcm, &s) == 0) {
        out.data[out.size * 3 / 4] ^= 0x5a;
        decode(&d, &out, NULL, 2, 0, 0);
        check(d.errors > 0, "a damaged frame is reported");
        release(&d);
    } else {
        check(0, "encoding the stream to damage");
    }
    free(out.data);
    free(pcm);
}

int main(void) {
    version();
    round_trip(2, 16, 5, 0, "377a68fd75c2834e8aefc4f1b51da072");
    round_trip(2, 24, 8, 0, "396866600755368e0bd5a4546e94e858");
    round_trip(1, 32, 5, 0, "31d78ff66550a46166353d93b7333d60");
    round_trip(2, 16, 5, 1, "377a68fd75c2834e8aefc4f1b51da072");
    metadata_and_seeking();
    multithreaded();
    files();
    damaged();
    printf("libflac (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
