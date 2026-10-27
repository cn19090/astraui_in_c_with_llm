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

/* 后端不分配内存：实例和画布都由调用者提供。测试里共用一块静态实例，
   每次 hal_vbuf_init() 都会把它整体清零。 */
static hal_vbuf s_vbuf;

static hal *vbuf_reset(void) { return hal_vbuf_init(&s_vbuf); }

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
  uint8_t last[ASTRA_PANEL_CANVAS_BYTES];
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

static void test_panel_geometry(void) {
  hal *h = vbuf_reset();
  CHECK(h != NULL);
  CHECK(hal_inject(h));

  CHECK(strcmp(hal_type(), "VBUF") == 0);
  CHECK(hal_get_buffer_tile_width() == ASTRA_PANEL_TILE_W);
  CHECK(hal_get_buffer_tile_height() == ASTRA_PANEL_TILE_H);
  CHECK(hal_system_config()->screen_weight == ASTRA_PANEL_WIDTH);
  CHECK(hal_system_config()->screen_height == ASTRA_PANEL_HEIGHT);
  CHECK(hal_get_canvas_buffer() != NULL);
  CHECK(hal_vbuf_canvas() != NULL);
  CHECK(hal_vbuf_width() == ASTRA_PANEL_WIDTH);
  CHECK(hal_vbuf_height() == ASTRA_PANEL_HEIGHT);
  CHECK(hal_get_font_height() == ASTRA_VBUF_FONT_H);

  /* The canvas is exactly one page per tile: no slack, no rounding. */
  CHECK((size_t) ASTRA_PANEL_TILE_W * ASTRA_PANEL_TILE_H * 8u ==
        (size_t) ASTRA_PANEL_CANVAS_BYTES);

  hal_destroy();
  printf("ok  panel %s geometry: %d tiles, %d canvas bytes, %d px font\n",
         ASTRA_PANEL_NAME, ASTRA_PANEL_TILE_W * ASTRA_PANEL_TILE_H,
         ASTRA_PANEL_CANVAS_BYTES, ASTRA_VBUF_FONT_H);
}

/* ------------------------------------------------------------------------ */
/* canvas                                                                    */
/* ------------------------------------------------------------------------ */

static void test_canvas_clear_and_layout(void) {
  hal_inject(vbuf_reset());
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();
  const size_t bytes = ASTRA_PANEL_CANVAS_BYTES;

  CHECK(count_lit(buf, bytes) == 0); /* init() zeroes the embedded canvas */

  hal_set_draw_type(1);
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(pixel_at(buf, ASTRA_PANEL_WIDTH, 0, 0));
  CHECK(!pixel_at(buf, ASTRA_PANEL_WIDTH, 1, 0));
  /* Row-major 1bpp: byte 0, bit 7 is the top-left pixel. */
  CHECK(buf[0] == 0x80);

  hal_draw_pixel(8.0f, 0.0f); /* first pixel of the next byte */
  CHECK(buf[1] == 0x80);

  hal_draw_pixel((float) (ASTRA_PANEL_WIDTH - 1), (float) (ASTRA_PANEL_HEIGHT - 1));
  CHECK(buf[bytes - 1] == 0x01);

  hal_canvas_clear();
  CHECK(count_lit(buf, bytes) == 0);

  hal_destroy();
  printf("ok  canvas uses the 1bpp page layout of an SSD1306\n");
}

static void test_draw_type(void) {
  hal_inject(vbuf_reset());
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();

  hal_set_draw_type(1);
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(pixel_at(buf, ASTRA_PANEL_WIDTH, 0, 0));

  hal_set_draw_type(0); /* clear mode */
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(!pixel_at(buf, ASTRA_PANEL_WIDTH, 0, 0));

  hal_set_draw_type(1);
  hal_draw_pixel(0.0f, 0.0f);
  hal_set_draw_type(2); /* invert mode */
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(!pixel_at(buf, ASTRA_PANEL_WIDTH, 0, 0));

  hal_set_draw_type(1);
  hal_destroy();
  printf("ok  draw_type 0/1/2 clear, set and invert\n");
}

static void test_clipping(void) {
  hal_inject(vbuf_reset());
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();
  const size_t bytes = ASTRA_PANEL_CANVAS_BYTES;

  hal_set_draw_type(1);
  hal_draw_pixel(-5.0f, -5.0f);
  hal_draw_pixel(500.0f, 500.0f);
  hal_draw_pixel((float) (ASTRA_PANEL_WIDTH - 1) + 0.4f,
                 (float) (ASTRA_PANEL_HEIGHT - 1) + 0.4f); /* rounds onto the last pixel */
  hal_draw_h_line((float) (ASTRA_PANEL_WIDTH - 8), 10.0f,
                  40.0f); /* 只有最后 8 个像素落在屏内 */
  CHECK(count_lit(buf, bytes) == 9); /* 1 个像素 + x = width-8 .. width-1 */

  hal_destroy();
  printf("ok  drawing outside the panel is clipped, not corrupting\n");
}

static void test_primitives(void) {
  hal_inject(vbuf_reset());
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();

  hal_set_draw_type(1);
  hal_draw_box(10.0f, 10.0f, 4.0f, 3.0f);
  CHECK(pixel_at(buf, ASTRA_PANEL_WIDTH, 10, 10));
  CHECK(pixel_at(buf, ASTRA_PANEL_WIDTH, 13, 12));
  CHECK(!pixel_at(buf, ASTRA_PANEL_WIDTH, 14, 12));

  hal_canvas_clear();
  hal_draw_frame(10.0f, 10.0f, 4.0f, 4.0f);
  CHECK(pixel_at(buf, ASTRA_PANEL_WIDTH, 10, 10));  /* corner */
  CHECK(pixel_at(buf, ASTRA_PANEL_WIDTH, 13, 13));  /* corner */
  CHECK(!pixel_at(buf, ASTRA_PANEL_WIDTH, 11, 11)); /* hollow */

  hal_canvas_clear();
  hal_draw_h_dotted_line(0.0f, 0.0f, 16.0f);
  CHECK(!pixel_at(buf, ASTRA_PANEL_WIDTH, 0, 0)); /* gap  */
  CHECK(pixel_at(buf, ASTRA_PANEL_WIDTH, 3, 0));  /* dash */
  CHECK(!pixel_at(buf, ASTRA_PANEL_WIDTH, 8, 0)); /* gap  */

  /* Bitmap: 8 wide, 2 rows, 1 byte per row. */
  static const uint8_t bmp[2] = {0x80, 0x01};
  hal_canvas_clear();
  hal_draw_bmp(0.0f, 0.0f, 8.0f, 2.0f, bmp);
  CHECK(pixel_at(buf, ASTRA_PANEL_WIDTH, 0, 0));
  CHECK(pixel_at(buf, ASTRA_PANEL_WIDTH, 7, 1));

  hal_destroy();
  printf("ok  box/frame/dotted line/bitmap primitives\n");
}

static void test_text(void) {
  hal_inject(vbuf_reset());
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();
  const size_t bytes = ASTRA_PANEL_CANVAS_BYTES;

  CHECK(hal_get_font_width("") == 0);
  CHECK(hal_get_font_width("A") == ASTRA_VBUF_FONT_W);
  CHECK(hal_get_font_width("System") == 6 * ASTRA_VBUF_FONT_W);

  hal_set_draw_type(1);
  hal_draw_english(0.0f, (float) ASTRA_VBUF_FONT_H, "A");
  CHECK(count_lit(buf, bytes) > 5);       /* the glyph is not blank */
  CHECK(count_lit(buf, bytes) < 60);      /* and it is a glyph, not a box */

  hal_canvas_clear();
  hal_draw_chinese(0.0f, (float) ASTRA_VBUF_FONT_H, "\xE4\xBD\xA0"); /* a CJK code point */
  CHECK(count_lit(buf, bytes) > 20);             /* placeholder box drawn */
  CHECK(count_lit(buf, bytes) < 80);

  /* All three roles must be accepted and must not change the metrics: this
     backend only owns one 6x12 cell, so the request is recorded and ignored. */
  hal_set_font(ASTRA_FONT_TITLE);
  CHECK(hal_get_font_height() == ASTRA_VBUF_FONT_H);
  CHECK(hal_get_font_width("A") == ASTRA_VBUF_FONT_W);
  hal_set_font(ASTRA_FONT_SMALL);
  CHECK(hal_get_font_height() == ASTRA_VBUF_FONT_H);
  hal_set_font(ASTRA_FONT_BODY);
  CHECK(hal_get_font_height() == ASTRA_VBUF_FONT_H);

  hal_destroy();
  printf("ok  text metrics and glyph rasterisation\n");
}

/* ------------------------------------------------------------------------ */
/* flush hook, clock                                                         */
/* ------------------------------------------------------------------------ */

static void test_flush_hook(void) {
  hal_inject(vbuf_reset());

  flush_probe probe;
  memset(&probe, 0, sizeof(probe));
  hal_vbuf_set_flush(probe_flush, &probe);

  hal_set_draw_type(1);
  hal_draw_pixel(3.0f, 4.0f);
  hal_canvas_update();

  CHECK(probe.calls == 1);
  CHECK(probe.width == ASTRA_PANEL_WIDTH);
  CHECK(probe.height == ASTRA_PANEL_HEIGHT);
  CHECK(pixel_at(probe.last, ASTRA_PANEL_WIDTH, 3, 4)); /* the host received the frame */

  hal_canvas_update();
  CHECK(probe.calls == 2);

  hal_vbuf_set_flush(NULL, NULL);
  hal_canvas_update();
  CHECK(probe.calls == 2); /* unsubscribed */

  hal_destroy();
  printf("ok  canvas_update hands the frame to the flush hook\n");
}

static void test_clock(void) {
  hal_inject(vbuf_reset());

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
  hal_inject(vbuf_reset());
  const uint8_t *buf = (const uint8_t *) hal_vbuf_canvas();
  const size_t bytes = ASTRA_PANEL_CANVAS_BYTES;

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

static void test_embedded_canvas(void) {
  static hal_vbuf vbuf;

  hal *h = hal_vbuf_init(&vbuf);
  CHECK(h == &vbuf.base);
  CHECK(hal_inject(h));

  /* 画布是实例的成员，不是另外分配的指针。 */
  CHECK(hal_vbuf_canvas() == vbuf.buffer);
  CHECK(sizeof(vbuf.buffer) == ASTRA_PANEL_CANVAS_BYTES);

  hal_set_draw_type(1);
  hal_draw_pixel(0.0f, 0.0f);
  CHECK(vbuf.buffer[0] == 0x80);

  hal_vbuf_deinit(&vbuf); /* 不释放任何东西：实例归调用者 */
  CHECK(vbuf.buffer[0] == 0x80);

  /* hal_destroy() 只是取消发布，不会 free() 静态实例；之后宿主 API 失效。 */
  hal_destroy();
  CHECK(hal_vbuf_canvas() == NULL);

  printf("ok  the canvas is embedded in the instance, nothing is freed\n");
}

static void test_repeated_cycles(void) {
  for (int i = 0; i < 5; ++i) {
    hal *h = vbuf_reset();
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

  test_panel_geometry();
  test_canvas_clear_and_layout();
  test_draw_type();
  test_clipping();
  test_primitives();
  test_text();
  test_flush_hook();
  test_clock();
  test_screen_off();
  test_embedded_canvas();
  test_repeated_cycles();
  test_host_api_without_backend();

  printf("\n%s (%d failure%s)\n", s_failures == 0 ? "PASS" : "FAIL", s_failures,
         s_failures == 1 ? "" : "s");
  return s_failures == 0 ? 0 : 1;
}
