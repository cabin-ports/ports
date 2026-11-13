// Consumer test for cabin-ports/cute_headers from C: collision queries, a PNG
// round trip, an Aseprite file built in memory, sprite batching, threads with
// an atomic counter and a mutex, and a cute_net connect token generated,
// read and verified.  cute_tiled.h compiles only as C++, so main.cc tests it.

// glibc declares pthread_setname_np, which cute_sync.h's POSIX backend calls,
// only under _GNU_SOURCE.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define CUTE_ASEPRITE_IMPLEMENTATION
#include <cute_aseprite.h>
#define CUTE_C2_IMPLEMENTATION
#include <cute_c2.h>
#define CUTE_NET_IMPLEMENTATION
#include <cute_net.h>
#define CUTE_PNG_IMPLEMENTATION
#include <cute_png.h>
#define SPRITEBATCH_IMPLEMENTATION
#include <cute_spritebatch.h>
#ifdef _WIN32
#define CUTE_SYNC_WINDOWS
#else
#define CUTE_SYNC_POSIX
#endif
#define CUTE_SYNC_IMPLEMENTATION
#include <cute_sync.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int pass, const char *what) {
  if (!pass) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static int close_to(float a, float b) { return a - b < 1e-5f && b - a < 1e-5f; }

static void test_c2(void) {
  c2Circle a, b, distant;
  c2AABB box, overlapping, apart;
  c2Manifold m;
  a.p = c2V(0, 0);
  a.r = 1;
  b.p = c2V(1.5f, 0);
  b.r = 1;
  distant.p = c2V(3, 0);
  distant.r = 1;
  check(c2CircletoCircle(a, b), "overlapping circles collide");
  check(!c2CircletoCircle(a, distant), "distant circles do not collide");
  c2CircletoCircleManifold(a, b, &m);
  check(m.count == 1 && close_to(m.depths[0], 0.5f) && close_to(m.n.x, 1) && close_to(m.n.y, 0),
        "the circle manifold has depth 0.5 along +x");

  box.min = c2V(0, 0);
  box.max = c2V(2, 2);
  overlapping.min = c2V(1, 1);
  overlapping.max = c2V(3, 3);
  apart.min = c2V(2.5f, 0);
  apart.max = c2V(4, 2);
  check(c2AABBtoAABB(box, overlapping) && !c2AABBtoAABB(box, apart), "boxes collide only when they overlap");
}

static void test_png(void) {
  static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  cp_image_t img = cp_load_blank(3, 2);
  cp_saved_png_t saved;
  cp_image_t back;
  int i;
  for (i = 0; i < 6; ++i) {
    img.pix[i].r = (uint8_t)(i * 40);
    img.pix[i].g = (uint8_t)(255 - i * 40);
    img.pix[i].b = (uint8_t)i;
    img.pix[i].a = (uint8_t)(i == 5 ? 0x80 : 0xff);
  }
  saved = cp_save_png_to_memory(&img);
  check(saved.data != NULL && saved.size > 8 && memcmp(saved.data, signature, 8) == 0, "the PNG starts with its signature");
  back = cp_load_png_mem(saved.data, saved.size);
  check(back.w == 3 && back.h == 2 && back.pix != NULL && memcmp(back.pix, img.pix, sizeof(cp_pixel_t) * 6) == 0,
        "a saved PNG loads back with the same pixels");
  free(saved.data);
  cp_free_png(&img);
  cp_free_png(&back);
}

static unsigned char *put16(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)(v & 0xff);
  p[1] = (unsigned char)(v >> 8);
  return p + 2;
}

static unsigned char *put32(unsigned char *p, unsigned long v) {
  p = put16(p, (unsigned)(v & 0xffff));
  return put16(p, (unsigned)(v >> 16));
}

// A 2x2 RGBA sprite with one visible layer "bg" and one raw cel, laid out as
// the Aseprite file format specifies.
static void test_aseprite(void) {
  static const unsigned char pixels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0};
  unsigned char file[212];
  unsigned char *p;
  ase_t *ase;
  memset(file, 0, sizeof(file));
  p = put32(file, sizeof(file));
  p = put16(p, 0xA5E0); // magic
  p = put16(p, 1);      // frames
  p = put16(p, 2);      // width
  p = put16(p, 2);      // height
  p = put16(p, 32);     // bits per pixel
  p = put32(p, 1);      // flags: layer opacity is valid
  p = put16(p, 100);    // default frame duration

  p = put32(file + 128, sizeof(file) - 128); // frame size
  p = put16(p, 0xF1FA);
  p = put16(p, 2);  // chunk count
  p = put16(p, 50); // frame duration
  p = put16(p, 0);
  p = put32(p, 2);

  p = put32(p, 26);     // layer chunk size
  p = put16(p, 0x2004); // layer chunk
  p = put16(p, 1);      // flags: visible
  p = put16(p, 0);      // normal layer
  p = put16(p, 0);      // child level
  p = put16(p, 0);      // default width
  p = put16(p, 0);      // default height
  p = put16(p, 0);      // normal blend mode
  *p++ = 255;           // opacity
  p += 3;
  p = put16(p, 2);
  memcpy(p, "bg", 2);
  p += 2;

  p = put32(p, 42);     // cel chunk size
  p = put16(p, 0x2005); // cel chunk
  p = put16(p, 0);      // layer index
  p = put16(p, 0);      // x
  p = put16(p, 0);      // y
  *p++ = 255;           // opacity
  p = put16(p, 0);      // raw cel
  p += 7;
  p = put16(p, 2); // cel width
  p = put16(p, 2); // cel height
  memcpy(p, pixels, sizeof(pixels));
  p += sizeof(pixels);
  check(p == file + sizeof(file), "the Aseprite test file is laid out completely");

  ase = cute_aseprite_load_from_memory(file, (int)sizeof(file), NULL);
  check(ase->w == 2 && ase->h == 2 && ase->mode == ASE_MODE_RGBA, "the sprite is 2x2 RGBA");
  check(ase->layer_count == 1 && strcmp(ase->layers[0].name, "bg") == 0, "the layer is read with its name");
  check(ase->frame_count == 1 && ase->frames[0].duration_milliseconds == 50, "the frame keeps its duration");
  check(memcmp(ase->frames[0].pixels, pixels, sizeof(pixels)) == 0, "the frame shows the cel's pixels");
  cute_aseprite_free(ase);
}

typedef struct batch_log {
  int batches, sprites, textures, bad_pixels, pair_4x4, single_8x2;
} batch_log;

static void sb_submit(spritebatch_sprite_t *sprites, int count, int texture_w, int texture_h, void *udata) {
  batch_log *log = (batch_log *)udata;
  (void)sprites;
  ++log->batches;
  log->sprites += count;
  if (texture_w == 4 && texture_h == 4 && count == 2) log->pair_4x4 = 1;
  if (texture_w == 8 && texture_h == 2 && count == 1) log->single_8x2 = 1;
}

static void sb_get_pixels(SPRITEBATCH_U64 image_id, void *buffer, int bytes_to_fill, void *udata) {
  (void)udata;
  memset(buffer, (int)image_id * 16, (size_t)bytes_to_fill);
}

static SPRITEBATCH_U64 sb_generate(void *pixels, int w, int h, void *udata) {
  batch_log *log = (batch_log *)udata;
  const unsigned char *px = (const unsigned char *)pixels;
  unsigned char expected = (unsigned char)(w == 4 ? 16 : 32);
  int i;
  for (i = 0; i < w * h * 4; ++i) {
    if (px[i] != expected) ++log->bad_pixels;
  }
  return (SPRITEBATCH_U64)++log->textures;
}

static void sb_destroy(SPRITEBATCH_U64 texture_id, void *udata) {
  (void)texture_id;
  (void)udata;
}

static spritebatch_sprite_t sprite(SPRITEBATCH_U64 image_id, int w, int h, float x) {
  spritebatch_sprite_t s;
  memset(&s, 0, sizeof(s));
  s.image_id = image_id;
  s.w = w;
  s.h = h;
  s.geom.x = x;
  s.geom.sx = 1;
  s.geom.sy = 1;
  s.geom.c = 1;
  return s;
}

// Image 1 is 4x4 and image 2 is 8x2; each becomes its own texture, so the
// flush reports one batch per texture.
static void test_spritebatch(void) {
  spritebatch_config_t config;
  spritebatch_t sb;
  batch_log log;
  memset(&log, 0, sizeof(log));
  spritebatch_set_default_config(&config);
  config.batch_callback = sb_submit;
  config.get_pixels_callback = sb_get_pixels;
  config.generate_texture_callback = sb_generate;
  config.delete_texture_callback = sb_destroy;
  check(spritebatch_init(&sb, &config, &log) == 0, "the sprite batcher initializes");
  spritebatch_push(&sb, sprite(1, 4, 4, 0));
  spritebatch_push(&sb, sprite(1, 4, 4, 10));
  spritebatch_push(&sb, sprite(2, 8, 2, 20));
  spritebatch_tick(&sb);
  spritebatch_defrag(&sb);
  check(spritebatch_flush(&sb) == 2, "the flush reports two batches");
  check(log.batches == 2 && log.sprites == 3 && log.pair_4x4 && log.single_8x2,
        "sprites are batched by texture with each image's size");
  check(log.textures == 2 && log.bad_pixels == 0, "each texture is made from its image's pixels");
  spritebatch_term(&sb);
}

typedef struct work {
  cute_atomic_int_t *counter;
  cute_mutex_t *mutex;
  int *total;
} work;

static int worker(void *udata) {
  work *w = (work *)udata;
  int i;
  for (i = 0; i < 1000; ++i) {
    cute_atomic_add(w->counter, 1);
    cute_lock(w->mutex);
    ++*w->total;
    cute_unlock(w->mutex);
  }
  return 7;
}

static void test_sync(void) {
  cute_atomic_int_t counter;
  cute_mutex_t mutex = cute_mutex_create();
  cute_thread_t *threads[4];
  int total = 0;
  int codes = 0;
  int i;
  work w;
  memset(&counter, 0, sizeof(counter));
  check(cute_atomic_set(&counter, 100) == 0, "setting the counter returns its old value");
  w.counter = &counter;
  w.mutex = &mutex;
  w.total = &total;
  for (i = 0; i < 4; ++i) threads[i] = cute_thread_create(worker, "worker", &w);
  for (i = 0; i < 4; ++i) codes += cute_thread_wait(threads[i]);
  check(codes == 28, "each thread returns its code");
  check(cute_atomic_get(&counter) == 4100 && total == 4000, "four threads add 1000 each");
  cute_mutex_destroy(&mutex);
}

// cute_net without sockets, as upstream's cn_test_generate_connect_token: the
// client reads the token's public part, and the server decrypts and verifies
// the part the client would send.
static void test_net(void) {
  const char *endpoints[] = {"127.0.0.1:37001", "[::1]:37002"};
  cn_crypto_key_t client_to_server = cn_crypto_generate_key();
  cn_crypto_key_t server_to_client = cn_crypto_generate_key();
  cn_crypto_sign_public_t pk;
  cn_crypto_sign_secret_t sk;
  uint8_t token_buffer[CN_CONNECT_TOKEN_SIZE];
  uint8_t tampered[CN_CONNECT_TOKEN_SIZE];
  cn_protocol_connect_token_t token;
  cn_protocol_connect_token_decrypted_t decrypted;
  cn_endpoint_t endpoint[2];
  char text[64];
  uint8_t *packet;

  check(cn_endpoint_init(&endpoint[0], endpoints[0]) == 0 && cn_endpoint_init(&endpoint[1], endpoints[1]) == 0,
        "IPv4 and IPv6 endpoints parse");
  check(endpoint[0].type == CN_ADDRESS_TYPE_IPV4 && endpoint[0].port == 37001 && endpoint[1].type == CN_ADDRESS_TYPE_IPV6 &&
            endpoint[1].port == 37002 && !cn_endpoint_equals(endpoint[0], endpoint[1]),
        "the endpoints keep their address types and ports");
  cn_endpoint_to_string(endpoint[0], text, (int)sizeof(text));
  check(strcmp(text, endpoints[0]) == 0, "an IPv4 endpoint prints back as written");

  cn_crypto_sign_keygen(&pk, &sk);
  check(!cn_is_error(cn_generate_connect_token(333, 100, &client_to_server, &server_to_client, 200, 5, 2, endpoints, 17,
                                               NULL, &sk, token_buffer)),
        "a connect token is generated");
  memcpy(tampered, token_buffer, sizeof(tampered)); // decryption works in place

  packet = cn_protocol_client_read_connect_token_from_web_service(token_buffer, 333, 100, &token);
  check(packet != NULL, "the client reads the token");
  if (!packet) return;
  check(token.creation_timestamp == 100 && token.expiration_timestamp == 200 && token.handshake_timeout == 5 &&
            token.endpoint_count == 2 && cn_endpoint_equals(token.endpoints[0], endpoint[0]) &&
            cn_endpoint_equals(token.endpoints[1], endpoint[1]) &&
            memcmp(&token.client_to_server_key, &client_to_server, sizeof(client_to_server)) == 0,
        "the token's public part holds what was generated");

  check(!cn_is_error(cn_protocol_server_decrypt_connect_token_packet(packet, &pk, &sk, 333, 100, &decrypted)),
        "the server decrypts the token");
  check(decrypted.client_id == 17 && decrypted.endpoint_count == 2 &&
            memcmp(&decrypted.client_to_server_key, &client_to_server, sizeof(client_to_server)) == 0 &&
            memcmp(&decrypted.server_to_client_key, &server_to_client, sizeof(server_to_client)) == 0,
        "the decrypted part holds the client id and both keys");

  packet = cn_protocol_client_read_connect_token_from_web_service(tampered, 333, 100, &token);
  check(packet != NULL, "the client reads the copy");
  if (!packet) return;
  packet[100] ^= 1;
  check(cn_is_error(cn_protocol_server_decrypt_connect_token_packet(packet, &pk, &sk, 333, 100, &decrypted)),
        "a token with one bit changed is rejected");
}

int main(void) {
  test_c2();
  test_png();
  test_aseprite();
  test_spritebatch();
  test_sync();
  test_net();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("cute_headers 2026.11.3 C ok");
  return 0;
}
