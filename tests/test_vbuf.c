//
// astra - memory-only backend self-test.
//
// Covers the parts of hal_vbuf that a host depends on: canvas geometry, the
// 1bpp page layout, drawing primitives, the flush hook, the virtual clock,
// static storage and teardown. Runs headless, links no graphics library.
//
// Build: cmake -S Inc/astra -B Inc/astra/build    (standalone)
//        cmake -S . -B build -DASTRA_BUILD_TESTS=ON   (from the repo root)
// Run:   astra_test_vbuf.exe
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal.h"
#include "hal_vbuf.h"

#ifdef _WIN32
#include <crtdbg.h>
#endif

static int s_failures = 0;

#define CHECK(cond)                                            \
  do {                                                         \
    if (!(cond)) {                                             \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
      s_failures++;                                            \
    }                                                          \
  } while (0)

/* ------------------------------------------------------------------------ */
/* helpers                                                                   */
/* ------------------------------------------------------------------------ */

static bool pixel_at(const uint8_t *buf, int width, int x, int y) {
  const uint8_t page = buf[(y * width + x) / 8];
  return ((page >> (7 - (x % 8))) & 1u) != 0;
}

static int count_lit(const uint8_t *buf, size_t bytes) {
  int n = 0;
  for (size_t i = 0; i < bytes; ++i)
    for (int b = 0; b < 8; ++b)
      if ((buf[i] >> b) & 1u) n++;
  return n;
}

/* Flush hook bookkeeping. */
typedef struct {
  int calls;
  int width;
  int height;
  uint8_t last[128 * 64 / 8];
} flush_probe;

static void probe_flush(void *user, const uint8_t *buffer, int width, int height) {
  flush_probe *p = (flush_probe *) user;
  p->calls++;
  p->width = width;
  p->height = height;
  memcpy(p->last, buffer, sizeof(p->last));
}

/* ------------------------------------------------------------------------ */
/* construction and geometry                                                 */
/* ------------------------------------------------------------------------ */

static void test_default_geometry(void) {
  hal *h = hal_vbuf_create(NULL);
  CHECK(h != NULL);
  CHECK(hal_inject(h));

  CHECK(strcmp(hal_type(), "VBUF") == 0);
  CHECK(hal_get_buffer_tile_width() == 16);   /* 128 / 8 */
  CHECK(hal_get_buffer_tile_height() == 8);   /* 64 / 8  */
  CHECK(hal_system_config()->screen_weight == 128);
  CHECK(hal_system_config()->screen_height == 64);
  CHECK(hal_get_canvas_buffer() != NULL);
  CHECK(hal_vbuf_canvas() != NULL);
  CHECK(hal_vbuf_width() == 128);
  CHECK(hal_vbuf_height() == 64);
  CHECK(hal_get_font_height() == 12);

  hal_destroy();
  printf("ok  default geometry is 128x64 with 16x8 tiles\n");
}

static void test_custom_geometry(void) {
  hal_vbuf_config cfg;
  hal_vbuf_config_default(&cfg);
  CHECK(cfg.width == 128);
  CHECK(cfg.height == 64);

  cfg.width = 64;
  cfg.height = 48;
  hal *h = hal_vbuf_create(&cfg);
  CHECK(h != NULL);
  CHECK(hal_inject(h));
  CHECK(hal_get_buffer_tile_width() == 8);
  CHECK(hal_get_buffer_tile_height() == 6);
  CHECK(hal_system_config()->screen_weight == 64);
  CHECK(hal_system_config()->screen_height == 48);
  hal_destroy();

  printf("ok  a custom panel size is honoured\n");
}

static void test_invalid_geometry(void) {
  hal_vbuf_config cfg;

  hal_vbuf_config_default(&cfg);
  cfg.width = 100; /* not a multiple of 8 */
  CHECK(hal_vbuf_create(&cfg) == NULL);

  hal_vbuf_config_default(&cfg);
  cfg.width = -8;
  CHECK(hal_vbuf_create(&cfg) == NULL);

  hal_vbuf_config_default(&cfg);
  cfg.height = 4096; /* beyond the uint8_t tile contract */
  CHECK(hal_vbuf_create(&cfg) == NULL);

  /* Zeroed fields fall back to the 128x64 default. */
  hal_vbuf_config_default(&cfg);
  cfg.width = 0;
  cfg.height = 0;
  hal *h = hal_vbuf_create(&cfg);
  CHECK(h != NULL);
  CHECK(hal_inject(h));
  CHECK(hal_vbuf_width() == 128);
  CHECK(hal_vbuf_height() == 64);
  hal_destroy();

  printf("ok  invalid panel sizes are rejected, zero falls back\n");
}

/* ------------------------------------------------------------------------ */
/* canvas                                                                    */
/* ------------------------------------------------------------------------ */

static void test_canvas_clear_and_layout(void) {
  hal_inject(hal_vbuf_create(NULL));
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();
  const size_t bytes = 128 * 64 / 8;

  CHECK(count_lit(buf, bytes) == 0); /* calloc'd */

  hal_set_draw_type(1);
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(pixel_at(buf, 128, 0, 0));
  CHECK(!pixel_at(buf, 128, 1, 0));
  /* Byte 0, bit 7 is the top-left pixel: the SSD1306 page layout. */
  CHECK(buf[0] == 0x80);

  hal_draw_pixel(8.0f, 0.0f); /* first pixel of the next byte */
  CHECK(buf[1] == 0x80);

  hal_draw_pixel(127.0f, 63.0f); /* last pixel of the last page */
  CHECK(buf[bytes - 1] == 0x01);

  hal_canvas_clear();
  CHECK(count_lit(buf, bytes) == 0);

  hal_destroy();
  printf("ok  canvas uses the 1bpp page layout of an SSD1306\n");
}

static void test_draw_type(void) {
  hal_inject(hal_vbuf_create(NULL));
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();

  hal_set_draw_type(1);
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(pixel_at(buf, 128, 0, 0));

  hal_set_draw_type(0); /* clear mode */
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(!pixel_at(buf, 128, 0, 0));

  hal_set_draw_type(1);
  hal_draw_pixel(0.0f, 0.0f);
  hal_set_draw_type(2); /* invert mode */
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(!pixel_at(buf, 128, 0, 0));

  hal_set_draw_type(1);
  hal_destroy();
  printf("ok  draw_type 0/1/2 clear, set and invert\n");
}

static void test_clipping(void) {
  hal_inject(hal_vbuf_create(NULL));
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();
  const size_t bytes = 128 * 64 / 8;

  hal_set_draw_type(1);
  hal_draw_pixel(-5.0f, -5.0f);
  hal_draw_pixel(500.0f, 500.0f);
  hal_draw_pixel(127.4f, 63.4f); /* rounds to 127,63 -> the last visible pixel */
  hal_draw_h_line(120.0f, 10.0f, 40.0f); /* runs off the right edge */
  CHECK(count_lit(buf, bytes) == 9);     /* 1 pixel + x = 120..127 */

  hal_destroy();
  printf("ok  drawing outside the panel is clipped, not corrupting\n");
}

static void test_primitives(void) {
  hal_inject(hal_vbuf_create(NULL));
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();

  hal_set_draw_type(1);
  hal_draw_box(10.0f, 10.0f, 4.0f, 3.0f);
  CHECK(pixel_at(buf, 128, 10, 10));
  CHECK(pixel_at(buf, 128, 13, 12));
  CHECK(!pixel_at(buf, 128, 14, 12));

  hal_canvas_clear();
  hal_draw_frame(10.0f, 10.0f, 4.0f, 4.0f);
  CHECK(pixel_at(buf, 128, 10, 10));  /* corner */
  CHECK(pixel_at(buf, 128, 13, 13));  /* corner */
  CHECK(!pixel_at(buf, 128, 11, 11)); /* hollow */

  hal_canvas_clear();
  hal_draw_h_dotted_line(0.0f, 0.0f, 16.0f);
  CHECK(!pixel_at(buf, 128, 0, 0)); /* gap  */
  CHECK(pixel_at(buf, 128, 3, 0));  /* dash */
  CHECK(!pixel_at(buf, 128, 8, 0)); /* gap  */

  /* Bitmap: 8 wide, 2 rows, 1 byte per row. */
  static const uint8_t bmp[2] = {0x80, 0x01};
  hal_canvas_clear();
  hal_draw_bmp(0.0f, 0.0f, 8.0f, 2.0f, bmp);
  CHECK(pixel_at(buf, 128, 0, 0));
  CHECK(pixel_at(buf, 128, 7, 1));

  hal_destroy();
  printf("ok  box/frame/dotted line/bitmap primitives\n");
}

static void test_text(void) {
  hal_inject(hal_vbuf_create(NULL));
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();
  const size_t bytes = 128 * 64 / 8;

  CHECK(hal_get_font_width("") == 0);
  CHECK(hal_get_font_width("A") == 6);
  CHECK(hal_get_font_width("System") == 36);

  hal_set_draw_type(1);
  hal_draw_english(0.0f, 12.0f, "A");
  CHECK(count_lit(buf, bytes) > 5);       /* the glyph is not blank */
  CHECK(count_lit(buf, bytes) < 60);      /* and it is a glyph, not a box */

  hal_canvas_clear();
  hal_draw_chinese(0.0f, 12.0f, "\xE4\xBD\xA0"); /* a CJK code point */
  CHECK(count_lit(buf, bytes) > 20);             /* placeholder box drawn */
  CHECK(count_lit(buf, bytes) < 80);

  hal_destroy();
  printf("ok  text metrics and glyph rasterisation\n");
}

/* ------------------------------------------------------------------------ */
/* flush hook, clock                                                         */
/* ------------------------------------------------------------------------ */

static void test_flush_hook(void) {
  hal_inject(hal_vbuf_create(NULL));

  flush_probe probe;
  memset(&probe, 0, sizeof(probe));
  hal_vbuf_set_flush(probe_flush, &probe);

  hal_set_draw_type(1);
  hal_draw_pixel(3.0f, 4.0f);
  hal_canvas_update();

  CHECK(probe.calls == 1);
  CHECK(probe.width == 128);
  CHECK(probe.height == 64);
  CHECK(pixel_at(probe.last, 128, 3, 4)); /* the host received the frame */

  hal_canvas_update();
  CHECK(probe.calls == 2);

  hal_vbuf_set_flush(NULL, NULL);
  hal_canvas_update();
  CHECK(probe.calls == 2); /* unsubscribed */

  hal_destroy();
  printf("ok  canvas_update hands the frame to the flush hook\n");
}

static void test_clock(void) {
  hal_inject(hal_vbuf_create(NULL));

  CHECK(hal_millis() == 0);

  hal_vbuf_advance(16);
  CHECK(hal_millis() == 16);

  hal_delay(100); /* virtual: advances instead of blocking */
  CHECK(hal_millis() == 116);

  hal_vbuf_advance(16);
  CHECK(hal_get_tick() == 132);

  /* The random seed must not be constant, or the logo stars never move. */
  const unsigned long a = hal_get_random_seed();
  const unsigned long b = hal_get_random_seed();
  CHECK(a != b);

  hal_destroy();
  printf("ok  virtual clock advances without blocking\n");
}

static void test_screen_off(void) {
  hal_inject(hal_vbuf_create(NULL));
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();
  const size_t bytes = 128 * 64 / 8;

  hal_set_draw_type(1);
  hal_draw_box(0.0f, 0.0f, 10.0f, 10.0f);
  CHECK(count_lit(buf, bytes) > 0);

  hal_screen_off();
  CHECK(count_lit(buf, bytes) == 0); /* blanked */

  hal_draw_box(0.0f, 0.0f, 10.0f, 10.0f);
  CHECK(count_lit(buf, bytes) == 0); /* drawing is dropped while off */

  hal_screen_on();
  hal_draw_box(0.0f, 0.0f, 10.0f, 10.0f);
  CHECK(count_lit(buf, bytes) > 0);

  hal_destroy();
  printf("ok  screen_on/off gate drawing and blank the canvas\n");
}

/* ------------------------------------------------------------------------ */
/* ownership                                                                 */
/* ------------------------------------------------------------------------ */

static void test_static_storage(void) {
  static hal_vbuf vbuf;
  static uint8_t canvas[64 * 64 / 8];

  /* Too small on purpose: must be refused rather than overflow. */
  hal_vbuf_config cfg;
  hal_vbuf_config_default(&cfg);
  cfg.width = 128;
  cfg.height = 64;
  CHECK(hal_vbuf_init_static(&vbuf, &cfg, canvas, sizeof(canvas)) == NULL);

  cfg.width = 64;
  cfg.height = 64;
  hal *h = hal_vbuf_init_static(&vbuf, &cfg, canvas, sizeof(canvas));
  CHECK(h == &vbuf.base);
  CHECK(hal_inject(h));
  CHECK(hal_vbuf_width() == 64);
  CHECK(hal_vbuf_canvas() == canvas); /* the caller's array, not a copy */

  hal_set_draw_type(1);
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(canvas[0] == 0x80);

  hal_vbuf_deinit(&vbuf); /* releases nothing: the array stays ours */
  CHECK(canvas[0] == 0x80);

  printf("ok  static storage uses the caller's array and is not freed\n");
}

static void test_repeated_cycles(void) {
  for (int i = 0; i < 5; ++i) {
    hal *h = hal_vbuf_create(NULL);
    CHECK(h != NULL);
    CHECK(hal_inject(h));
    CHECK(hal_vbuf_canvas() != NULL);
    hal_destroy();
    CHECK(!hal_check());
    CHECK(hal_vbuf_canvas() == NULL); /* host API is inert without a HAL */
  }

  printf("ok  create/destroy cycles leak nothing and leave no stale HAL\n");
}

static void test_host_api_without_backend(void) {
  /* No HAL injected: every host accessor must be a safe no-op. */
  CHECK(hal_vbuf_canvas() == NULL);
  CHECK(hal_vbuf_width() == 0);
  CHECK(hal_vbuf_height() == 0);
  hal_vbuf_set_flush(probe_flush, NULL);
  hal_vbuf_advance(10);

  printf("ok  host API is inert when no vbuf backend is injected\n");
}

int main(void) {
#ifdef _WIN32
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
#endif

  test_default_geometry();
  test_custom_geometry();
  test_invalid_geometry();
  test_canvas_clear_and_layout();
  test_draw_type();
  test_clipping();
  test_primitives();
  test_text();
  test_flush_hook();
  test_clock();
  test_screen_off();
  test_static_storage();
  test_repeated_cycles();
  test_host_api_without_backend();

  printf("\n%s (%d failure%s)\n", s_failures == 0 ? "PASS" : "FAIL", s_failures,
         s_failures == 1 ? "" : "s");
  return s_failures == 0 ? 0 : 1;
}
