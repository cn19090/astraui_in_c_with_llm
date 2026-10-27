//
// astra - memory-only (software) hardware abstraction layer.
//
// Unlike the raylib / SSD1306 backends, this backend drives **nothing**. Every
// operation writes into a plain 1bpp page buffer that lives in RAM. Nothing is
// sent anywhere: you decide when and how to push that buffer to a real panel
// (or to a file, a framebuffer, an emulator, ...).
//
// The canvas is exactly the panel selected in astra_panel.h, so the size is
// fixed at build time and there is no configuration to pass in:
//
//   -DASTRA_PANEL=ASTRA_PANEL_128x64   (default)  canvas 1024 B
//   -DASTRA_PANEL=ASTRA_PANEL_128x32             canvas  512 B
//   -DASTRA_PANEL=ASTRA_PANEL_128x128            canvas 2048 B
//   -DASTRA_PANEL=ASTRA_PANEL_240x240            canvas 7200 B
//
//   hal *h = hal_vbuf_create();          /* heap: struct + canvas */
//   hal_inject(h);
//   ... run the UI ...
//   const uint8_t *buf = hal_vbuf_canvas();  /* your turn to flush it */
//   hal_destroy();
//
// On an MCU, supply the canvas yourself so nothing touches the heap:
//
//   static hal_vbuf s_vbuf;
//   static uint8_t  s_canvas[ASTRA_PANEL_CANVAS_BYTES];
//   hal_vbuf_init_static(&s_vbuf, s_canvas, sizeof(s_canvas));
//   hal_inject(&s_vbuf.base);
//   ...
//   hal_vbuf_deinit(&s_vbuf);
//
// A flush callback can be installed so canvas_update() hands the finished frame
// straight to your transport:
//
//   hal_vbuf_set_flush(my_i2c_send, &my_bus);
//
// The buffer is a plain **row-major** 1bpp bitmap: one bit per pixel, row y
// occupies bytes [y * width / 8, (y + 1) * width / 8), byte index
// (y * width + x) / 8, bit 7 = leftmost pixel.
//
// NOTE: this is *not* the SSD1306 GDDRAM layout (page-major, byte index
// (y / 8) * width + x, bit 0 = top of the page). A driver for a real panel has
// to transpose the buffer before sending it; see the flush callback below.
//
#ifndef ASTRA_HAL_VBUF_H_
#define ASTRA_HAL_VBUF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "astra_panel.h"
#include "hal.h"

/* Called at the end of every hal_canvas_update(), i.e. once per UI frame.
   `buffer` is the 1bpp page buffer described at the top of this file. It stays
   valid until the next canvas_update(), so a transport may keep a pointer. */
typedef void (*hal_vbuf_flush_fn)(void *user, const uint8_t *buffer,
                                  int width, int height);

/* Instance layout is public so the backend can live in static storage. `base`
   must stay the first member. Prefer the constructors below over poking at
   these fields directly. */
typedef struct hal_vbuf {
  hal base; /* must be first: the ops table casts hal* back to hal_vbuf* */

  uint8_t *buffer; /* ASTRA_PANEL_CANVAS_BYTES, row-major 1bpp */
  size_t buffer_bytes;
  bool owns_buffer; /* true => deinit() frees the buffer */

  uint8_t draw_type; /* 0 clear, 1 solid, 2 invert */
  bool display_on;   /* false => drawing is dropped, like a blanked panel */

  astra_font_role font; /* last role requested; see ASTRA_VBUF_FONT_* */

  hal_vbuf_flush_fn flush;
  void *flush_user;

  unsigned long tick_ms; /* virtual clock, advanced by delay/advance */
  uint32_t rng;
} hal_vbuf;

/* Heap constructor: allocates the instance and its canvas for the selected
   panel. Returns a hal that must be handed to hal_inject(); hal_destroy()
   frees everything. NULL when out of memory. */
hal *hal_vbuf_create(void);

/* Static constructor: `canvas` must hold at least ASTRA_PANEL_CANVAS_BYTES
   bytes. hal_inject() it like any other backend; release with
   hal_vbuf_deinit() (the array stays yours) instead of hal_destroy().
   Returns &storage->base, or NULL when the buffer is too small. */
hal *hal_vbuf_init_static(hal_vbuf *storage, uint8_t *canvas,
                          size_t canvas_bytes);

/* Static teardown counterpart of hal_vbuf_init_static(). Safe to call twice.
   For heap instances use hal_destroy(). */
void hal_vbuf_deinit(hal_vbuf *vbuf);

/* ------------------------------------------------------------------------ */
/* host facing API - only valid while this backend is the injected HAL       */
/* ------------------------------------------------------------------------ */

/* The live 1bpp page buffer, or NULL when no vbuf backend is injected.
   Read it after hal_canvas_update() and push it wherever you like. */
const uint8_t *hal_vbuf_canvas(void);

/* Panel geometry of the live buffer (0 when no vbuf backend is injected).
   These are the compile-time panel constants, exposed for convenience. */
int hal_vbuf_width(void);
int hal_vbuf_height(void);

/* Install (or clear, with NULL) the frame callback described above. */
void hal_vbuf_set_flush(hal_vbuf_flush_fn fn, void *user);

/* Advance the virtual clock without blocking. Call once per frame with the
   real elapsed time if you want delays to behave. delay_ms() advances the same
   clock, so a headless run completes instantly. */
void hal_vbuf_advance(unsigned long ms);

#endif /* ASTRA_HAL_VBUF_H_ */
