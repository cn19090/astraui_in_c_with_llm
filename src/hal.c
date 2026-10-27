//
// astra - hardware abstraction layer.
//
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "astra_config.h"

/* ------------------------------------------------------------------------ */
/* singleton                                                                 */
/* ------------------------------------------------------------------------ */

static hal *s_hal = NULL;

/* ------------------------------------------------------------------------ */
/* default backend - every capability is a safe no-op                       */
/* ------------------------------------------------------------------------ */

static const char *default_type(hal *self) {
  (void) self;
  return "Base";
}
static void default_noop_init(hal *self) { (void) self; }
static void default_noop_deinit(hal *self) { (void) self; }
static void *default_get_canvas_buffer(hal *self) {
  (void) self;
  return NULL;
}
static uint8_t default_u8_zero(hal *self) {
  (void) self;
  return 0;
}
static void default_void(hal *self) { (void) self; }
static void default_set_font(hal *self, const uint8_t *font) {
  (void) self;
  (void) font;
}
static uint8_t default_font_width(hal *self, const char *text) {
  (void) self;
  (void) text;
  return 0;
}
static void default_set_draw_type(hal *self, uint8_t type) {
  (void) self;
  (void) type;
}
static void default_draw_xy(hal *self, float x, float y) {
  (void) self;
  (void) x;
  (void) y;
}
static void default_draw_text(hal *self, float x, float y, const char *text) {
  (void) self;
  (void) x;
  (void) y;
  (void) text;
}
static void default_draw_len(hal *self, float x, float y, float len) {
  (void) self;
  (void) x;
  (void) y;
  (void) len;
}
static void default_draw_bmp(hal *self, float x, float y, float w, float h,
                             const uint8_t *bmp) {
  (void) self;
  (void) x;
  (void) y;
  (void) w;
  (void) h;
  (void) bmp;
}
static void default_draw_rect(hal *self, float x, float y, float w, float h) {
  (void) self;
  (void) x;
  (void) y;
  (void) w;
  (void) h;
}
static void default_draw_rrect(hal *self, float x, float y, float w, float h,
                               float r) {
  (void) self;
  (void) x;
  (void) y;
  (void) w;
  (void) h;
  (void) r;
}
static void default_delay(hal *self, unsigned long ms) {
  (void) self;
  (void) ms;
}
static unsigned long default_ul_zero(hal *self) {
  (void) self;
  return 0;
}
static void default_beep(hal *self, float freq) {
  (void) self;
  (void) freq;
}
static void default_beep_vol(hal *self, uint8_t vol) {
  (void) self;
  (void) vol;
}

/**
 * @brief Auto-wrapping log output: keeps the last N lines on screen.
 * @note Blocking (it renders and presents). Do not call it from a render loop.
 */
void hal_default_print_info(hal *self, const char *msg) {
  static char s_cache[8][48];
  static uint8_t s_count = 0;

  const uint8_t font_height = self->ops->get_font_height(self);
  const uint8_t max_lines =
      font_height == 0 ? 8 : (uint8_t) (self->config.screen_height / font_height);

  if (s_count >= max_lines) s_count = 0;
  if (s_count < 8) {
    snprintf(s_cache[s_count], sizeof(s_cache[0]), "%s", msg == NULL ? "" : msg);
    s_count++;
  }

  self->ops->canvas_clear(self);
  self->ops->set_draw_type(self, 2); /* inverted */
  for (uint8_t i = 0; i < s_count; ++i)
    self->ops->draw_english(self, 0.0f,
                            (float) (font_height + i * (1 + font_height)),
                            s_cache[i]);
  self->ops->canvas_update(self);
  self->ops->set_draw_type(self, 1); /* back to solid */
}

const hal_ops astra_hal_default_ops = {
    .type = default_type,
    .init = default_noop_init,
    .deinit = default_noop_deinit,

    .get_canvas_buffer = default_get_canvas_buffer,
    .get_buffer_tile_width = default_u8_zero,
    .get_buffer_tile_height = default_u8_zero,
    .canvas_update = default_void,
    .canvas_clear = default_void,

    .set_font = default_set_font,
    .get_font_width = default_font_width,
    .get_font_height = default_u8_zero,
    .set_draw_type = default_set_draw_type,

    .draw_pixel = default_draw_xy,
    .draw_english = default_draw_text,
    .draw_chinese = default_draw_text,
    .draw_v_dotted_line = default_draw_len,
    .draw_h_dotted_line = default_draw_len,
    .draw_v_line = default_draw_len,
    .draw_h_line = default_draw_len,
    .draw_bmp = default_draw_bmp,
    .draw_box = default_draw_rect,
    .draw_rbox = default_draw_rrect,
    .draw_frame = default_draw_rect,
    .draw_rframe = default_draw_rrect,

    .delay_ms = default_delay,
    .millis = default_ul_zero,
    .get_tick = default_ul_zero,
    .get_random_seed = default_ul_zero,

    .beep = default_beep,
    .beep_stop = default_void,
    .set_beep_vol = default_beep_vol,
    .screen_on = default_void,
    .screen_off = default_void,

    .print_info = hal_default_print_info,
    .update_config = default_void,
};

/* ------------------------------------------------------------------------ */
/* singleton management                                                      */
/* ------------------------------------------------------------------------ */

static void hal_construct_impl(hal *h, const hal_ops *ops, bool owned) {
  if (h == NULL) return;

  memset(h, 0, sizeof(*h));
  h->ops = ops == NULL ? &astra_hal_default_ops : ops;
  h->config = astra_sys_config_default();
  h->heap_owned = owned;
}

void hal_construct(hal *h, const hal_ops *ops) {
  hal_construct_impl(h, ops, false);
}

void hal_construct_owned(hal *h, const hal_ops *ops) {
  hal_construct_impl(h, ops, true);
}

hal *hal_get(void) { return s_hal; }

bool hal_check(void) { return s_hal != NULL; }

bool hal_inject(hal *h) {
  if (h == NULL || h->ops == NULL) return false;

  /* init() runs before the singleton is published, so a backend must not call
     hal_get() from inside its own init(). */
  h->ops->init(h);
  s_hal = h;
  return true;
}

void hal_destroy(void) {
  if (s_hal == NULL) return;

  hal *h = s_hal;
  s_hal = NULL; /* clear first so deinit() cannot re-enter into a dead HAL */
  if (h->ops != NULL && h->ops->deinit != NULL) h->ops->deinit(h);

  /* Release GPU/text/font resources above (deinit) BEFORE freeing the struct
     that describes them. Static backends are left alone. */
  if (h->heap_owned) free(h);
}
