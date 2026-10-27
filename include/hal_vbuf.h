//
// astra - memory-only (software) hardware abstraction layer.
//
// Unlike the raylib / SSD1306 backends, this backend drives **nothing**. Every
// operation writes into a plain 1bpp page buffer that lives in RAM. Nothing is
// sent anywhere: you decide when and how to push that buffer to a real panel
// (or to a file, a framebuffer, an emulator, ...).
//
//   hal *h = hal_vbuf_create(NULL);        /* 128x64, heap owned */
//   hal_inject(h);
//   ... run the UI ...
//   const uint8_t *buf = hal_vbuf_canvas();  /* your turn to flush it */
//   hal_destroy();
//
// Two ways to own the buffer:
//
//   hal_vbuf_create(&cfg)          heap allocates struct + canvas; release
//                                  everything with hal_destroy().
//   hal_vbuf_init_static(...)      you supply a static array (typical on an
//                                  MCU); release with hal_vbuf_deinit() and
//                                  the array stays yours.
//
// A flush callback can be installed so canvas_update() hands the finished
// frame straight to your transport:
//
//   hal_vbuf_set_flush(my_i2c_send, &my_bus);
//
// The buffer layout is exactly an SSD1306 GDDRAM: one bit per pixel, page
// major, byte index (y * width + x) / 8, bit 7 = leftmost pixel. So the usual
// "set page, write 128 bytes" loop works on it unmodified.
//
#ifndef ASTRA_HAL_VBUF_H_
#define ASTRA_HAL_VBUF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal.h"

/* Font handles. All three map to the same built-in 6x12 ASCII table in this
   backend; they exist so the application can stay backend agnostic. */
extern const uint8_t hal_vbuf_font_main;
extern const uint8_t hal_vbuf_font_logo_title;
extern const uint8_t hal_vbuf_font_logo_copyright;

typedef struct hal_vbuf_config {
  int width;  /* panel width in pixels, multiple of 8; 0 => 128 */
  int height; /* panel height in pixels, multiple of 8; 0 => 64 */
} hal_vbuf_config;

/* Called at the end of every hal_canvas_update(), i.e. once per UI frame.
   `buffer` is the 1bpp page buffer described at the top of this file. It stays
   valid until the next canvas_update(), so a transport may keep a pointer. */
typedef void (*hal_vbuf_flush_fn)(void *user, const uint8_t *buffer,
                                  int width, int height);

/* Instance layout is public so a backend can live in static storage. `base`
   must stay the first member. Prefer the constructors below over poking at
   these fields directly. */
typedef struct hal_vbuf {
  hal base; /* must be first: the ops table casts hal* back to hal_vbuf* */

  int width;
  int height;
  int tile_width;  /* width  / 8 */
  int tile_height; /* height / 8 */

  uint8_t *buffer;   /* tile_width * tile_height * 8 bytes, 1bpp pages */
  size_t buffer_bytes;
  bool owns_buffer;  /* true => deinit() frees the buffer */

  uint8_t draw_type;  /* 0 clear, 1 solid, 2 invert */
  bool display_on;    /* false => drawing is dropped, like a blanked panel */

  const uint8_t *font; /* one of the three handles above; NULL => main */

  hal_vbuf_flush_fn flush;
  void *flush_user;

  unsigned long tick_ms; /* virtual clock, advanced by delay/advance */
  uint32_t rng;
} hal_vbuf;

/* Fills `cfg` with the 128x64 default and zeroes the rest. */
void hal_vbuf_config_default(hal_vbuf_config *cfg);

/* Heap constructor: allocates the instance and its canvas. Returns a hal that
   must be handed to hal_inject(); hal_destroy() frees everything. NULL on OOM
   or an invalid size (must be > 0, a multiple of 8, and at most 2040x2040). */
hal *hal_vbuf_create(const hal_vbuf_config *cfg);

/* Static constructor: `storage` is your array of `storage_bytes`, which must
   hold at least width * height / 8 bytes. hal_inject() it like any other
   backend; release with hal_vbuf_deinit() (the array stays yours) instead of
   hal_destroy(). Returns `&storage->base`, or NULL if the size is invalid or
   the storage is too small. */
hal *hal_vbuf_init_static(hal_vbuf *storage, const hal_vbuf_config *cfg,
                          uint8_t *canvas, size_t canvas_bytes);

/* Static teardown counterpart of hal_vbuf_init_static(). Safe to call twice.
   For heap instances use hal_destroy(). */
void hal_vbuf_deinit(hal_vbuf *vbuf);

/* ------------------------------------------------------------------------ */
/* host facing API - only valid while this backend is the injected HAL       */
/* ------------------------------------------------------------------------ */

/* The live 1bpp page buffer, or NULL when no vbuf backend is injected.
   Read it after hal_canvas_update() and push it wherever you like. */
const uint8_t *hal_vbuf_canvas(void);

/* Geometry of the live buffer (0 when no vbuf backend is injected). */
int hal_vbuf_width(void);
int hal_vbuf_height(void);

/* Install (or clear, with NULL) the frame callback described above. */
void hal_vbuf_set_flush(hal_vbuf_flush_fn fn, void *user);

/* Advance the virtual clock without blocking. Call once per frame with the
   real elapsed time if you want delays to behave. delay_ms() advances the same
   clock, so a headless run completes instantly. */
void hal_vbuf_advance(unsigned long ms);

#endif /* ASTRA_HAL_VBUF_H_ */
