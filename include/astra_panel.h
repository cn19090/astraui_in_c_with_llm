//
// astra - panel selection.
//
// The framework is compiled for exactly one panel geometry. Pick it with a
// single macro and everything else (canvas size, font sizes, layout profile,
// sample-app icon scale) follows:
//
//   cmake -S Inc/astra -B build -DASTRA_PANEL=ASTRA_PANEL_128x32
//   cl /DASTRA_PANEL=ASTRA_PANEL_240x240 ...
//
// Supported panels:
//
//   ASTRA_PANEL_128x64    SSD1306 / SSD1315, the reference layout
//   ASTRA_PANEL_128x32    SSD1306 half height (wide and short)
//   ASTRA_PANEL_128x128   square 1.5" panels
//   ASTRA_PANEL_240x240   large square panels (ST7701/GC9A01 class)
//
// Why compile-time instead of a runtime width/height pair: every layout
// constant, the built-in bitmap font and the sample icons are all sized for one
// geometry. Choosing at build time lets each panel carry numbers picked for it
// instead of a generic set scaled by a factor, which is both smaller and
// sharper. Adding a panel means adding one #elif block below plus a case in
// astra_config_default().
//
#ifndef ASTRA_PANEL_H_
#define ASTRA_PANEL_H_

/* Panel ids. These are plain integers so they can be compared in #if. */
#define ASTRA_PANEL_128x64 1
#define ASTRA_PANEL_128x32 2
#define ASTRA_PANEL_128x128 3
#define ASTRA_PANEL_240x240 4

/* Default: the reference 128x64 module. */
#ifndef ASTRA_PANEL
#define ASTRA_PANEL ASTRA_PANEL_128x64
#endif

/* ------------------------------------------------------------------------ */
/* 128 x 64 - reference                                                     */
/* ------------------------------------------------------------------------ */
#if ASTRA_PANEL == ASTRA_PANEL_128x64

#define ASTRA_PANEL_WIDTH 128
#define ASTRA_PANEL_HEIGHT 64
#define ASTRA_PANEL_NAME "128x64"

/* Font pixel heights the backend should aim for. */
#define ASTRA_FONT_TITLE_PX 14
#define ASTRA_FONT_BODY_PX 12
#define ASTRA_FONT_SMALL_PX 8

/* Built-in software-backend font table (see hal_vbuf.c). */
#define ASTRA_VBUF_FONT_SUFFIX 6X12
#define ASTRA_VBUF_FONT_W 6
#define ASTRA_VBUF_FONT_H 12
#define ASTRA_VBUF_FONT_ASCENT 10

/* The tile page fits a 30x30 icon plus a caption. */
#define ASTRA_PANEL_HAS_TILES 1
/* Integer upscale of the sample icons (1 = draw the 30x30 masters as-is). */
#define ASTRA_PANEL_ICON_SCALE 1

/* ------------------------------------------------------------------------ */
/* 128 x 32 - half height                                                   */
/* ------------------------------------------------------------------------ */
#elif ASTRA_PANEL == ASTRA_PANEL_128x32

#define ASTRA_PANEL_WIDTH 128
#define ASTRA_PANEL_HEIGHT 32
#define ASTRA_PANEL_NAME "128x32"

#define ASTRA_FONT_TITLE_PX 10
#define ASTRA_FONT_BODY_PX 8
#define ASTRA_FONT_SMALL_PX 6

#define ASTRA_VBUF_FONT_SUFFIX 5X8
#define ASTRA_VBUF_FONT_W 5
#define ASTRA_VBUF_FONT_H 8
#define ASTRA_VBUF_FONT_ASCENT 7

/* 32 px cannot hold a 30x30 icon plus a caption, so the sample app starts on a
   list page instead of a tile page. */
#define ASTRA_PANEL_HAS_TILES 0
#define ASTRA_PANEL_ICON_SCALE 1

/* ------------------------------------------------------------------------ */
/* 128 x 128 - square                                                       */
/* ------------------------------------------------------------------------ */
#elif ASTRA_PANEL == ASTRA_PANEL_128x128

#define ASTRA_PANEL_WIDTH 128
#define ASTRA_PANEL_HEIGHT 128
#define ASTRA_PANEL_NAME "128x128"

#define ASTRA_FONT_TITLE_PX 22
#define ASTRA_FONT_BODY_PX 16
#define ASTRA_FONT_SMALL_PX 10

#define ASTRA_VBUF_FONT_SUFFIX 8X16
#define ASTRA_VBUF_FONT_W 8
#define ASTRA_VBUF_FONT_H 16
#define ASTRA_VBUF_FONT_ASCENT 13

#define ASTRA_PANEL_HAS_TILES 1
/* 30 px icons would be a speck on a 128 px panel: draw them 2x. */
#define ASTRA_PANEL_ICON_SCALE 2

/* ------------------------------------------------------------------------ */
/* 240 x 240 - large square                                                 */
/* ------------------------------------------------------------------------ */
#elif ASTRA_PANEL == ASTRA_PANEL_240x240

#define ASTRA_PANEL_WIDTH 240
#define ASTRA_PANEL_HEIGHT 240
#define ASTRA_PANEL_NAME "240x240"

#define ASTRA_FONT_TITLE_PX 32
#define ASTRA_FONT_BODY_PX 24
#define ASTRA_FONT_SMALL_PX 14

#define ASTRA_VBUF_FONT_SUFFIX 12X24
#define ASTRA_VBUF_FONT_W 12
#define ASTRA_VBUF_FONT_H 24
#define ASTRA_VBUF_FONT_ASCENT 20

#define ASTRA_PANEL_HAS_TILES 1
#define ASTRA_PANEL_ICON_SCALE 4

#else
#error "Unknown ASTRA_PANEL: use ASTRA_PANEL_128x64 / _128x32 / _128x128 / _240x240"
#endif

/* The canvas is handed out in 1bpp pages, and the HAL reports the tile counts
   as uint8_t. */
#if (ASTRA_PANEL_WIDTH % 8) != 0 || (ASTRA_PANEL_HEIGHT % 8) != 0
#error "ASTRA_PANEL dimensions must be multiples of 8"
#endif

#define ASTRA_PANEL_TILE_W (ASTRA_PANEL_WIDTH / 8)
#define ASTRA_PANEL_TILE_H (ASTRA_PANEL_HEIGHT / 8)
#define ASTRA_PANEL_CANVAS_BYTES \
  (ASTRA_PANEL_TILE_W * ASTRA_PANEL_TILE_H * 8)

#endif /* ASTRA_PANEL_H_ */
