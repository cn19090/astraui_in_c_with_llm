//
// astra - hardware abstraction layer.
//
// Every backend implements one hal_ops table. `struct hal` holds a pointer to
// it plus the per-instance state, so a backend embeds `hal` as its first member
// and casts the hal* back to its own type:
//
//   typedef struct { hal base; /* ...backend state... */ } hal_mine;
//   static void mine_draw_pixel(hal *self, float x, float y) {
//     hal_mine *m = (hal_mine *) self;
//   }
//
// Callers never touch the table directly; they use the inline wrappers at the
// bottom of this header (hal_draw_pixel(), hal_canvas_update(), ...).
//
#ifndef ASTRA_HAL_H_
#define ASTRA_HAL_H_

#include <stdbool.h>
#include <stdint.h>

#include "astra_types.h"

typedef struct hal hal;
typedef struct hal_ops hal_ops;

struct hal_ops {
  const char *(*type)(hal *self);

  /* Called by hal_inject(), after which HAL::get() is valid. */
  void (*init)(hal *self);
  /* Called by hal_destroy() before the instance is released. */
  void (*deinit)(hal *self);

  /* --- canvas (1bpp page layout, same as an SSD1306 buffer) --- */
  void *(*get_canvas_buffer)(hal *self);
  uint8_t (*get_buffer_tile_width)(hal *self);
  uint8_t (*get_buffer_tile_height)(hal *self);
  void (*canvas_update)(hal *self);
  void (*canvas_clear)(hal *self);

  /* --- font --- */
  void (*set_font)(hal *self, const uint8_t *font);
  uint8_t (*get_font_width)(hal *self, const char *text);
  uint8_t (*get_font_height)(hal *self);
  void (*set_draw_type)(hal *self, uint8_t type);

  /* --- drawing (text coordinates are the lower-left corner) --- */
  void (*draw_pixel)(hal *self, float x, float y);
  void (*draw_english)(hal *self, float x, float y, const char *text);
  void (*draw_chinese)(hal *self, float x, float y, const char *text);
  void (*draw_v_dotted_line)(hal *self, float x, float y, float h);
  void (*draw_h_dotted_line)(hal *self, float x, float y, float l);
  void (*draw_v_line)(hal *self, float x, float y, float h);
  void (*draw_h_line)(hal *self, float x, float y, float l);
  void (*draw_bmp)(hal *self, float x, float y, float w, float h,
                   const uint8_t *bmp);
  void (*draw_box)(hal *self, float x, float y, float w, float h);
  void (*draw_rbox)(hal *self, float x, float y, float w, float h, float r);
  void (*draw_frame)(hal *self, float x, float y, float w, float h);
  void (*draw_rframe)(hal *self, float x, float y, float w, float h, float r);

  /* --- timers --- */
  void (*delay_ms)(hal *self, unsigned long ms);
  unsigned long (*millis)(hal *self);
  unsigned long (*get_tick)(hal *self);
  unsigned long (*get_random_seed)(hal *self);

  /* --- optional peripherals --- */
  void (*beep)(hal *self, float freq);
  void (*beep_stop)(hal *self);
  void (*set_beep_vol)(hal *self, uint8_t vol);
  void (*screen_on)(hal *self);
  void (*screen_off)(hal *self);

  /* --- keys --- */
  bool (*get_key)(hal *self, astra_key_index index);
  bool (*get_any_key)(hal *self);
  void (*key_scan)(hal *self);
  void (*key_test)(hal *self);

  /* --- misc --- */
  void (*print_info)(hal *self, const char *msg);
  void (*update_config)(hal *self);
};

struct hal {
  const hal_ops *ops;                  /* must stay the first member */
  astra_sys_config config;
  astra_key_action key[ASTRA_KEY_NUM];
  bool heap_owned;                     /* true => hal_destroy() calls free() */
};

/* All-no-op table: use it for pure-C fakes/tests. */
extern const hal_ops astra_hal_default_ops;

/* Singleton management. */
bool hal_inject(hal *h);
hal *hal_get(void);
bool hal_check(void);
void hal_destroy(void);

/* Backend constructor: sets ops, default system config and key state.
   Must be called before hal_inject() (typically from <backend>_create()).

   Ownership: hal_destroy() only releases the instance when it was registered
   with hal_construct_owned(). Backends that live in static storage (typical on
   MCUs) must use hal_construct() so hal_destroy() does not free() them. */
void hal_construct(hal *h, const hal_ops *ops);

/* Same as hal_construct(), but marks the instance as heap owned (malloc/calloc)
   so hal_destroy() will free() it. */
void hal_construct_owned(hal *h, const hal_ops *ops);

/* Default implementations that backends may reuse (logging, key scanning). */
void hal_default_print_info(hal *self, const char *msg);
bool hal_default_get_any_key(hal *self);
void hal_default_key_scan(hal *self);
void hal_default_key_test(hal *self);

/* ------------------------------------------------------------------------ */
/* Static-forwarding API. All inline, so each call compiles to a single      */
/* indirect call and the call sites read like plain function calls.          */
/* ------------------------------------------------------------------------ */

static inline const char *hal_type(void) { return hal_get()->ops->type(hal_get()); }

static inline void *hal_get_canvas_buffer(void) {
  hal *h = hal_get();
  return h->ops->get_canvas_buffer(h);
}
static inline uint8_t hal_get_buffer_tile_width(void) {
  hal *h = hal_get();
  return h->ops->get_buffer_tile_width(h);
}
static inline uint8_t hal_get_buffer_tile_height(void) {
  hal *h = hal_get();
  return h->ops->get_buffer_tile_height(h);
}
static inline void hal_canvas_update(void) {
  hal *h = hal_get();
  h->ops->canvas_update(h);
}
static inline void hal_canvas_clear(void) {
  hal *h = hal_get();
  h->ops->canvas_clear(h);
}

static inline void hal_set_font(const uint8_t *font) {
  hal *h = hal_get();
  h->ops->set_font(h, font);
}
static inline uint8_t hal_get_font_width(const char *text) {
  hal *h = hal_get();
  return h->ops->get_font_width(h, text);
}
static inline uint8_t hal_get_font_height(void) {
  hal *h = hal_get();
  return h->ops->get_font_height(h);
}
static inline void hal_set_draw_type(uint8_t type) {
  hal *h = hal_get();
  h->ops->set_draw_type(h, type);
}

static inline void hal_draw_pixel(float x, float y) {
  hal *h = hal_get();
  h->ops->draw_pixel(h, x, y);
}
static inline void hal_draw_english(float x, float y, const char *text) {
  hal *h = hal_get();
  h->ops->draw_english(h, x, y, text);
}
static inline void hal_draw_chinese(float x, float y, const char *text) {
  hal *h = hal_get();
  h->ops->draw_chinese(h, x, y, text);
}
static inline void hal_draw_v_dotted_line(float x, float y, float h_len) {
  hal *h = hal_get();
  h->ops->draw_v_dotted_line(h, x, y, h_len);
}
static inline void hal_draw_h_dotted_line(float x, float y, float l) {
  hal *h = hal_get();
  h->ops->draw_h_dotted_line(h, x, y, l);
}
static inline void hal_draw_v_line(float x, float y, float h_len) {
  hal *h = hal_get();
  h->ops->draw_v_line(h, x, y, h_len);
}
static inline void hal_draw_h_line(float x, float y, float l) {
  hal *h = hal_get();
  h->ops->draw_h_line(h, x, y, l);
}
static inline void hal_draw_bmp(float x, float y, float w, float h_len,
                                const uint8_t *bmp) {
  hal *h = hal_get();
  h->ops->draw_bmp(h, x, y, w, h_len, bmp);
}
static inline void hal_draw_box(float x, float y, float w, float h_len) {
  hal *h = hal_get();
  h->ops->draw_box(h, x, y, w, h_len);
}
static inline void hal_draw_rbox(float x, float y, float w, float h_len, float r) {
  hal *h = hal_get();
  h->ops->draw_rbox(h, x, y, w, h_len, r);
}
static inline void hal_draw_frame(float x, float y, float w, float h_len) {
  hal *h = hal_get();
  h->ops->draw_frame(h, x, y, w, h_len);
}
static inline void hal_draw_rframe(float x, float y, float w, float h_len, float r) {
  hal *h = hal_get();
  h->ops->draw_rframe(h, x, y, w, h_len, r);
}

static inline void hal_delay(unsigned long ms) {
  hal *h = hal_get();
  h->ops->delay_ms(h, ms);
}
static inline unsigned long hal_millis(void) {
  hal *h = hal_get();
  return h->ops->millis(h);
}
static inline unsigned long hal_get_tick(void) {
  hal *h = hal_get();
  return h->ops->get_tick(h);
}
static inline unsigned long hal_get_random_seed(void) {
  hal *h = hal_get();
  return h->ops->get_random_seed(h);
}

static inline void hal_beep(float freq) {
  hal *h = hal_get();
  h->ops->beep(h, freq);
}
static inline void hal_beep_stop(void) {
  hal *h = hal_get();
  h->ops->beep_stop(h);
}
static inline void hal_set_beep_vol(uint8_t vol) {
  hal *h = hal_get();
  h->ops->set_beep_vol(h, vol);
}
static inline void hal_screen_on(void) {
  hal *h = hal_get();
  h->ops->screen_on(h);
}
static inline void hal_screen_off(void) {
  hal *h = hal_get();
  h->ops->screen_off(h);
}

static inline bool hal_get_key(astra_key_index index) {
  hal *h = hal_get();
  return h->ops->get_key(h, index);
}
static inline bool hal_get_any_key(void) {
  hal *h = hal_get();
  return h->ops->get_any_key(h);
}
static inline void hal_key_scan(void) {
  hal *h = hal_get();
  h->ops->key_scan(h);
}
static inline void hal_key_test(void) {
  hal *h = hal_get();
  h->ops->key_test(h);
}

static inline void hal_print_info(const char *msg) {
  hal *h = hal_get();
  h->ops->print_info(h, msg);
}
static inline void hal_update_config(void) {
  hal *h = hal_get();
  h->ops->update_config(h);
}

/* HAL::getSystemConfig() / setSystemConfig() equivalents. */
static inline astra_sys_config *hal_system_config(void) {
  return &hal_get()->config;
}
static inline void hal_set_system_config(astra_sys_config cfg) {
  hal_get()->config = cfg;
}

#endif /* ASTRA_HAL_H_ */
