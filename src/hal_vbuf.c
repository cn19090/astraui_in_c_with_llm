//
// astra - memory-only (software) hardware abstraction layer.
//
// Everything this backend does lands in a plain 1bpp page buffer. There is no
// display, no timer peripheral, no key GPIO: the host owns the buffer and any
// clock or input it wants to feed in. See include/hal_vbuf.h for the contract.
//
#include "hal_vbuf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Arm 目标上 printf() 走半主机（BKPT 0xAB），真机没有调试器接管时会表现为
   "halted due to breakpoint"。裸机默认关掉，主机侧测试仍保留。 */
#ifndef ASTRA_VBUF_USE_PRINTF
#if defined(__arm__) || defined(__ARM_ARCH)
#define ASTRA_VBUF_USE_PRINTF 0
#else
#define ASTRA_VBUF_USE_PRINTF 1
#endif
#endif

#if ASTRA_VBUF_USE_PRINTF
#include <stdio.h>
#endif

/* Built-in 6x12 ASCII font, range 0x20..0x7E, one byte per row, bit 7 = the
   leftmost pixel (only the top 6 bits are used). Rasterised once from a small monospace face so the software
   backend can draw text without carrying a font file around. */
#define VBUF_FONT_FIRST 32
#define VBUF_FONT_LAST 126
#define VBUF_FONT_COUNT (VBUF_FONT_LAST - VBUF_FONT_FIRST + 1)
#define VBUF_GLYPH_W 6
#define VBUF_GLYPH_H 12
#define VBUF_ASCENT 8 /* rows from the top of a glyph down to the baseline  */

static const uint8_t VBUF_FONT[VBUF_FONT_COUNT][VBUF_GLYPH_H] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' ' */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00}, /* '!' */
    {0x68, 0x28, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '"' */
    {0x28, 0x28, 0x7C, 0x28, 0x28, 0xFC, 0x48, 0x48, 0x00, 0x00, 0x00, 0x00}, /* '#' */
    {0x38, 0x70, 0x50, 0x70, 0x38, 0x1C, 0x2C, 0x78, 0x20, 0x00, 0x00, 0x00}, /* '$' */
    {0xE4, 0xA8, 0xE8, 0x10, 0x20, 0x3C, 0x54, 0xCC, 0x00, 0x00, 0x00, 0x00}, /* '%' */
    {0x78, 0x48, 0x58, 0x70, 0x74, 0xDC, 0xCC, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* '&' */
    {0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ''' */
    {0x18, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x10, 0x08, 0x00, 0x00}, /* '(' */
    {0x20, 0x10, 0x10, 0x18, 0x08, 0x08, 0x18, 0x10, 0x30, 0x20, 0x00, 0x00}, /* ')' */
    {0x10, 0x78, 0x38, 0x50, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '*' */
    {0x00, 0x00, 0x10, 0x10, 0x7C, 0x30, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00}, /* '+' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x10, 0x10, 0x60, 0x00, 0x00}, /* ',' */
    {0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '-' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00}, /* '.' */
    {0x08, 0x08, 0x18, 0x10, 0x10, 0x20, 0x20, 0x60, 0x40, 0x00, 0x00, 0x00}, /* '/' */
    {0x38, 0x6C, 0x4C, 0xDC, 0xF4, 0x44, 0x4C, 0x38, 0x00, 0x00, 0x00, 0x00}, /* '0' */
    {0x10, 0x70, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* '1' */
    {0x30, 0x48, 0x0C, 0x08, 0x18, 0x30, 0x60, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* '2' */
    {0x70, 0x08, 0x08, 0x18, 0x38, 0x0C, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* '3' */
    {0x18, 0x18, 0x28, 0x68, 0x48, 0xFC, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00}, /* '4' */
    {0x78, 0x40, 0x40, 0x70, 0x18, 0x0C, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* '5' */
    {0x18, 0x30, 0x40, 0x78, 0x4C, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00}, /* '6' */
    {0x7C, 0x0C, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00}, /* '7' */
    {0x38, 0x4C, 0x4C, 0x78, 0x38, 0x4C, 0x44, 0x78, 0x00, 0x00, 0x00, 0x00}, /* '8' */
    {0x38, 0x48, 0x44, 0x44, 0x7C, 0x0C, 0x08, 0x70, 0x00, 0x00, 0x00, 0x00}, /* '9' */
    {0x00, 0x00, 0x30, 0x10, 0x00, 0x00, 0x10, 0x30, 0x00, 0x00, 0x00, 0x00}, /* ':' */
    {0x00, 0x00, 0x30, 0x10, 0x00, 0x00, 0x10, 0x10, 0x10, 0x60, 0x00, 0x00}, /* ';' */
    {0x00, 0x00, 0x18, 0x30, 0x60, 0x60, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00}, /* '<' */
    {0x00, 0x00, 0x00, 0x7C, 0x00, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '=' */
    {0x00, 0x00, 0x20, 0x10, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00}, /* '>' */
    {0x38, 0x08, 0x08, 0x18, 0x30, 0x20, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00}, /* '?' */
    {0x3C, 0x44, 0x44, 0xB8, 0xA8, 0xAC, 0xBC, 0xA8, 0xC0, 0x68, 0x00, 0x00}, /* '@' */
    {0x10, 0x38, 0x28, 0x68, 0x4C, 0x7C, 0xC4, 0x84, 0x00, 0x00, 0x00, 0x00}, /* 'A' */
    {0x70, 0x48, 0x4C, 0x58, 0x7C, 0x44, 0x4C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* 'B' */
    {0x1C, 0x64, 0x40, 0x40, 0x40, 0x40, 0x60, 0x3C, 0x00, 0x00, 0x00, 0x00}, /* 'C' */
    {0x60, 0x48, 0x44, 0x44, 0x44, 0x44, 0x4C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* 'D' */
    {0x78, 0x60, 0x40, 0x78, 0x78, 0x40, 0x40, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'E' */
    {0x78, 0x60, 0x40, 0x60, 0x78, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00}, /* 'F' */
    {0x3C, 0x64, 0x40, 0xC0, 0xDC, 0xC4, 0x44, 0x3C, 0x00, 0x00, 0x00, 0x00}, /* 'G' */
    {0x44, 0x44, 0x44, 0x7C, 0x7C, 0x44, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00}, /* 'H' */
    {0x7C, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'I' */
    {0x78, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x70, 0x00, 0x00, 0x00, 0x00}, /* 'J' */
    {0x44, 0x48, 0x50, 0x60, 0x60, 0x50, 0x48, 0x4C, 0x00, 0x00, 0x00, 0x00}, /* 'K' */
    {0x40, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'L' */
    {0x44, 0xCC, 0xEC, 0xF4, 0xB4, 0x84, 0x84, 0x84, 0x00, 0x00, 0x00, 0x00}, /* 'M' */
    {0x44, 0x64, 0x64, 0x74, 0x54, 0x5C, 0x4C, 0x4C, 0x00, 0x00, 0x00, 0x00}, /* 'N' */
    {0x38, 0x6C, 0xC4, 0xC4, 0xC4, 0xC4, 0x4C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* 'O' */
    {0x70, 0x4C, 0x44, 0x4C, 0x78, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00}, /* 'P' */
    {0x38, 0x6C, 0xC4, 0xC4, 0xC4, 0xC4, 0x4C, 0x78, 0x10, 0x1C, 0x00, 0x00}, /* 'Q' */
    {0x70, 0x48, 0x4C, 0x48, 0x70, 0x48, 0x4C, 0x44, 0x00, 0x00, 0x00, 0x00}, /* 'R' */
    {0x38, 0x60, 0x40, 0x60, 0x18, 0x0C, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* 'S' */
    {0x7C, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00}, /* 'T' */
    {0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x4C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* 'U' */
    {0x84, 0xC4, 0x44, 0x4C, 0x68, 0x28, 0x38, 0x30, 0x00, 0x00, 0x00, 0x00}, /* 'V' */
    {0x84, 0x84, 0x84, 0xD4, 0xF4, 0x7C, 0x6C, 0x4C, 0x00, 0x00, 0x00, 0x00}, /* 'W' */
    {0x44, 0x4C, 0x28, 0x30, 0x30, 0x38, 0x4C, 0xC4, 0x00, 0x00, 0x00, 0x00}, /* 'X' */
    {0x84, 0x44, 0x68, 0x28, 0x30, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00}, /* 'Y' */
    {0x7C, 0x0C, 0x08, 0x10, 0x30, 0x20, 0x40, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'Z' */
    {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x30, 0x00}, /* '[' */
    {0x40, 0x20, 0x20, 0x30, 0x10, 0x10, 0x08, 0x08, 0x04, 0x00, 0x00, 0x00}, /* '\' */
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x38, 0x30, 0x00}, /* ']' */
    {0x10, 0x30, 0x28, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '^' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0xFC, 0x00}, /* '_' */
    {0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '`' */
    {0x00, 0x00, 0x78, 0x0C, 0x1C, 0x6C, 0x4C, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'a' */
    {0x40, 0x40, 0x78, 0x6C, 0x44, 0x44, 0x4C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* 'b' */
    {0x00, 0x00, 0x38, 0x60, 0x40, 0x40, 0x60, 0x3C, 0x00, 0x00, 0x00, 0x00}, /* 'c' */
    {0x0C, 0x0C, 0x3C, 0x4C, 0x4C, 0x4C, 0x4C, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'd' */
    {0x00, 0x00, 0x38, 0x4C, 0x7C, 0x78, 0x40, 0x3C, 0x00, 0x00, 0x00, 0x00}, /* 'e' */
    {0x1C, 0x30, 0x30, 0xFC, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00}, /* 'f' */
    {0x00, 0x00, 0x3C, 0x4C, 0x4C, 0x78, 0x40, 0x7C, 0x44, 0x7C, 0x30, 0x00}, /* 'g' */
    {0x40, 0x40, 0x78, 0x6C, 0x44, 0x44, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00}, /* 'h' */
    {0x30, 0x00, 0x70, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'i' */
    {0x18, 0x00, 0x78, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x78, 0x20, 0x00}, /* 'j' */
    {0x40, 0x40, 0x4C, 0x58, 0x70, 0x70, 0x58, 0x4C, 0x00, 0x00, 0x00, 0x00}, /* 'k' */
    {0x70, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'l' */
    {0x00, 0x00, 0xFC, 0xD4, 0xD4, 0xD4, 0xD4, 0xD4, 0x00, 0x00, 0x00, 0x00}, /* 'm' */
    {0x00, 0x00, 0x78, 0x6C, 0x44, 0x44, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00}, /* 'n' */
    {0x00, 0x00, 0x38, 0x4C, 0x44, 0xC4, 0x4C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* 'o' */
    {0x00, 0x00, 0x78, 0x6C, 0x44, 0x44, 0x4C, 0x78, 0x40, 0x40, 0x00, 0x00}, /* 'p' */
    {0x00, 0x00, 0x3C, 0x4C, 0x4C, 0x4C, 0x4C, 0x7C, 0x0C, 0x0C, 0x00, 0x00}, /* 'q' */
    {0x00, 0x00, 0x58, 0x64, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00}, /* 'r' */
    {0x00, 0x00, 0x38, 0x40, 0x70, 0x18, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00}, /* 's' */
    {0x20, 0x20, 0xFC, 0x20, 0x20, 0x20, 0x20, 0x1C, 0x00, 0x00, 0x00, 0x00}, /* 't' */
    {0x00, 0x00, 0x44, 0x44, 0x44, 0x44, 0x4C, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'u' */
    {0x00, 0x00, 0xC4, 0x4C, 0x68, 0x28, 0x38, 0x30, 0x00, 0x00, 0x00, 0x00}, /* 'v' */
    {0x00, 0x00, 0x84, 0x84, 0xD4, 0x74, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00}, /* 'w' */
    {0x00, 0x00, 0x44, 0x68, 0x30, 0x30, 0x68, 0x4C, 0x00, 0x00, 0x00, 0x00}, /* 'x' */
    {0x00, 0x00, 0x44, 0x4C, 0x68, 0x28, 0x38, 0x30, 0x30, 0x60, 0x00, 0x00}, /* 'y' */
    {0x00, 0x00, 0x7C, 0x08, 0x10, 0x30, 0x60, 0x7C, 0x00, 0x00, 0x00, 0x00}, /* 'z' */
    {0x18, 0x10, 0x30, 0x30, 0x60, 0x20, 0x30, 0x30, 0x10, 0x18, 0x00, 0x00}, /* '{' */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00}, /* '|' */
    {0x30, 0x10, 0x10, 0x10, 0x0C, 0x10, 0x10, 0x10, 0x10, 0x30, 0x00, 0x00}, /* '}' */
    {0x00, 0x00, 0x00, 0x00, 0x74, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '~' */
};

/* The three handles the framework treats as opaque font ids. All of them select
   the same table: a 6x12 cell is the only size this backend can rasterise. */
const uint8_t hal_vbuf_font_main = 0;
const uint8_t hal_vbuf_font_logo_title = 1;
const uint8_t hal_vbuf_font_logo_copyright = 2;

#define VBUF_MAX_DIM 2040 /* keeps tile counts inside the uint8_t HAL contract */

/* ------------------------------------------------------------------------ */
/* helpers                                                                   */
/* ------------------------------------------------------------------------ */

static int clamp_int(int v, int lo, int hi)
{
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static size_t canvas_bytes(int width, int height)
{
  return (size_t)(width / 8) * (size_t)(height / 8) * 8u;
}

static void config_default(hal_vbuf_config *cfg)
{
  if (cfg == NULL)
    return;
  memset(cfg, 0, sizeof(*cfg));
  cfg->width = 128;
  cfg->height = 64;
}

void hal_vbuf_config_default(hal_vbuf_config *cfg) { config_default(cfg); }

static bool config_is_valid(const hal_vbuf_config *cfg)
{
  if (cfg->width <= 0 || cfg->height <= 0)
    return false;
  if (cfg->width % 8 != 0 || cfg->height % 8 != 0)
    return false;
  if (cfg->width > VBUF_MAX_DIM || cfg->height > VBUF_MAX_DIM)
    return false;
  return true;
}

/* Nearest-neighbour plot honouring draw_type, the clip rect and display_on. */
static void plot(hal_vbuf *v, int x, int y)
{
  if (v->buffer == NULL || !v->display_on)
    return;
  if (x < 0 || y < 0 || x >= v->width || y >= v->height)
    return;

  uint8_t *page = &v->buffer[(y * v->width + x) / 8];
  const uint8_t mask = (uint8_t)(1u << (7 - (x % 8)));

  switch (v->draw_type)
  {
  case 0:
    *page &= (uint8_t)(~mask);
    break; /* clear  */
  case 2:
    *page ^= mask;
    break; /* invert */
  default:
    *page |= mask;
    break; /* solid  */
  }
}

/* Minimal UTF-8 decoder: returns the number of code points written, never more
   than `max`. Malformed bytes advance by one, which keeps the pen moving. */
static int decode_utf8(const char *text, uint32_t *out, int max)
{
  if (text == NULL || out == NULL || max <= 0)
    return 0;

  const unsigned char *s = (const unsigned char *)text;
  int n = 0;

  while (*s != '\0' && n < max)
  {
    uint32_t cp = *s;
    int len = 1;

    if ((*s & 0x80) == 0x00)
    {
      len = 1;
    }
    else if ((*s & 0xE0) == 0xC0)
    {
      len = 2;
      cp = *s & 0x1Fu;
    }
    else if ((*s & 0xF0) == 0xE0)
    {
      len = 3;
      cp = *s & 0x0Fu;
    }
    else if ((*s & 0xF8) == 0xF0)
    {
      len = 4;
      cp = *s & 0x07u;
    }
    else
    {
      cp = 0xFFFD;
      len = 1;
    }

    for (int i = 1; i < len; ++i)
    {
      if ((s[i] & 0xC0) != 0x80)
      { /* truncated sequence */
        cp = 0xFFFD;
        len = i;
        break;
      }
      cp = (cp << 6) | (uint32_t)(s[i] & 0x3F);
    }

    out[n++] = cp;
    s += len;
  }
  return n;
}

/* Hollow box for code points the built-in table cannot draw (CJK, ...). */
static void draw_missing_glyph(hal_vbuf *v, int x, int top)
{
  for (int row = 0; row < VBUF_GLYPH_H; ++row)
    for (int col = 0; col < VBUF_GLYPH_W; ++col)
    {
      const bool edge = row == 0 || row == VBUF_GLYPH_H - 1 ||
                        col == 0 || col == VBUF_GLYPH_W - 1;
      if (edge)
        plot(v, x + col, top + row);
    }
}

/* `baseline` is the lower-left corner, matching the HAL contract. */
static void draw_text(hal_vbuf *v, int baseline_x, int baseline,
                      const char *text)
{
  uint32_t cps[128];
  const int n = decode_utf8(text == NULL ? "" : text, cps, 128);
  const int top = baseline - VBUF_ASCENT;

  int pen_x = baseline_x;

  for (int i = 0; i < n; ++i)
  {
    const uint32_t cp = cps[i];

    if (cp >= VBUF_FONT_FIRST && cp <= VBUF_FONT_LAST)
    {
      const uint8_t *glyph = VBUF_FONT[cp - VBUF_FONT_FIRST];
      for (int row = 0; row < VBUF_GLYPH_H; ++row)
        for (int col = 0; col < VBUF_GLYPH_W; ++col)
          if ((glyph[row] & (0x80u >> col)) != 0)
            plot(v, pen_x + col, top + row);
    }
    else
    {
      draw_missing_glyph(v, pen_x, top);
    }

    pen_x += VBUF_GLYPH_W;
  }
}

static void fill_rounded(hal_vbuf *v, int x, int y, int w, int h, float rad)
{
  if (w <= 0 || h <= 0)
    return;

  float radius = rad;
  const float limit = (float)(w < h ? w : h) / 2.0f;
  if (radius > limit)
    radius = limit;
  if (radius < 0.0f)
    radius = 0.0f;

  for (int row = 0; row < h; ++row)
  {
    for (int col = 0; col < w; ++col)
    {
      if (radius <= 0.0f)
      {
        plot(v, x + col, y + row);
        continue;
      }

      float dx = 0.0f;
      float dy = 0.0f;
      if ((float)col < radius)
        dx = radius - (float)col;
      else if ((float)col > (float)(w - 1) - radius)
        dx = (float)col - ((float)(w - 1) - radius);
      if ((float)row < radius)
        dy = radius - (float)row;
      else if ((float)row > (float)(h - 1) - radius)
        dy = (float)row - ((float)(h - 1) - radius);

      if (dx * dx + dy * dy <= radius * radius)
        plot(v, x + col, y + row);
    }
  }
}

static void stroke_rounded(hal_vbuf *v, int x, int y, int w, int h, float rad)
{
  if (w <= 0 || h <= 0)
    return;

  float radius = rad;
  const float limit = (float)(w < h ? w : h) / 2.0f;
  if (radius > limit)
    radius = limit;
  if (radius < 0.0f)
    radius = 0.0f;

  for (int row = 0; row < h; ++row)
  {
    for (int col = 0; col < w; ++col)
    {
      const bool edge = row == 0 || row == h - 1 || col == 0 || col == w - 1;
      if (!edge)
        continue;

      if (radius <= 0.0f)
      {
        plot(v, x + col, y + row);
        continue;
      }

      float dx = 0.0f;
      float dy = 0.0f;
      if ((float)col < radius)
        dx = radius - (float)col;
      else if ((float)col > (float)(w - 1) - radius)
        dx = (float)col - ((float)(w - 1) - radius);
      if ((float)row < radius)
        dy = radius - (float)row;
      else if ((float)row > (float)(h - 1) - radius)
        dy = (float)row - ((float)(h - 1) - radius);

      if (dx * dx + dy * dy <= radius * radius)
        plot(v, x + col, y + row);
    }
  }
}

/* ------------------------------------------------------------------------ */
/* ops: identity and lifecycle                                               */
/* ------------------------------------------------------------------------ */

static const char *vbuf_type(hal *self)
{
  (void)self;
  return "VBUF";
}

static void vbuf_init(hal *self)
{
  hal_vbuf *v = (hal_vbuf *)self;

  /* Publish the geometry so the framework's layout code has something to read.
     init() runs before the singleton is published, so hal_get() is off limits
     here - write the embedded struct directly. */
  self->config.screen_weight = (uint16_t)v->width;
  self->config.screen_height = (uint16_t)v->height;
}

static void vbuf_deinit(hal *self)
{
  hal_vbuf *v = (hal_vbuf *)self;
  if (v->owns_buffer)
    free(v->buffer);
  v->buffer = NULL;
  v->buffer_bytes = 0;
  v->owns_buffer = false;
}

/* ------------------------------------------------------------------------ */
/* ops: canvas                                                               */
/* ------------------------------------------------------------------------ */

static void *vbuf_get_canvas_buffer(hal *self)
{
  return ((hal_vbuf *)self)->buffer;
}

static uint8_t vbuf_get_buffer_tile_width(hal *self)
{
  return (uint8_t)((hal_vbuf *)self)->tile_width;
}

static uint8_t vbuf_get_buffer_tile_height(hal *self)
{
  return (uint8_t)((hal_vbuf *)self)->tile_height;
}

static void vbuf_canvas_clear(hal *self)
{
  hal_vbuf *v = (hal_vbuf *)self;
  if (v->buffer != NULL)
    memset(v->buffer, 0, v->buffer_bytes);
}

/* No hardware to poke: hand the finished frame to the host callback, if any. */
static void vbuf_canvas_update(hal *self)
{
  hal_vbuf *v = (hal_vbuf *)self;
  if (v->flush != NULL && v->buffer != NULL)
    v->flush(v->flush_user, v->buffer, v->width, v->height);
}

/* ------------------------------------------------------------------------ */
/* ops: font                                                                 */
/* ------------------------------------------------------------------------ */

static void vbuf_set_font(hal *self, const uint8_t *font)
{
  ((hal_vbuf *)self)->font = font;
}

static uint8_t vbuf_get_font_width(hal *self, const char *text)
{
  (void)self;
  uint32_t cps[128];
  const int n = decode_utf8(text == NULL ? "" : text, cps, 128);
  const int width = n * VBUF_GLYPH_W;
  return (uint8_t)(width > 255 ? 255 : width);
}

static uint8_t vbuf_get_font_height(hal *self)
{
  (void)self;
  return VBUF_GLYPH_H;
}

static void vbuf_set_draw_type(hal *self, uint8_t type)
{
  ((hal_vbuf *)self)->draw_type = type;
}

/* ------------------------------------------------------------------------ */
/* ops: primitives                                                           */
/* ------------------------------------------------------------------------ */

static void vbuf_draw_pixel(hal *self, float x, float y)
{
  plot((hal_vbuf *)self, (int)lroundf(x), (int)lroundf(y));
}

static void vbuf_draw_english(hal *self, float x, float y, const char *text)
{
  draw_text((hal_vbuf *)self, (int)lroundf(x), (int)lroundf(y), text);
}

static void vbuf_draw_chinese(hal *self, float x, float y, const char *text)
{
  /* Same path: the table only covers ASCII, anything else becomes a box. */
  vbuf_draw_english(self, x, y, text);
}

static void vbuf_draw_v_line(hal *self, float x, float y, float len)
{
  hal_vbuf *v = (hal_vbuf *)self;
  const int px = (int)lroundf(x);
  const int py = (int)lroundf(y);
  const int n = (int)lroundf(len);
  for (int i = 0; i < n; ++i)
    plot(v, px, py + i);
}

static void vbuf_draw_h_line(hal *self, float x, float y, float len)
{
  hal_vbuf *v = (hal_vbuf *)self;
  const int px = (int)lroundf(x);
  const int py = (int)lroundf(y);
  const int n = (int)lroundf(len);
  for (int i = 0; i < n; ++i)
    plot(v, px + i, py);
}

static void vbuf_draw_v_dotted_line(hal *self, float x, float y, float len)
{
  hal_vbuf *v = (hal_vbuf *)self;
  const int px = (int)lroundf(x);
  const int py = (int)lroundf(y);
  const int n = (int)lroundf(len);
  for (int i = 0; i < n; ++i)
  {
    if ((i % 8 == 0) || ((i - 1) % 8 == 0) || ((i - 2) % 8 == 0))
      continue;
    plot(v, px, py + i);
  }
}

static void vbuf_draw_h_dotted_line(hal *self, float x, float y, float len)
{
  hal_vbuf *v = (hal_vbuf *)self;
  const int px = (int)lroundf(x);
  const int py = (int)lroundf(y);
  const int n = (int)lroundf(len);
  for (int i = 0; i < n; ++i)
  {
    if ((i % 8 == 0) || ((i - 1) % 8 == 0) || ((i - 2) % 8 == 0))
      continue;
    plot(v, px + i, py);
  }
}

static void vbuf_draw_bmp(hal *self, float x, float y, float w, float h,
                          const uint8_t *bmp)
{
  hal_vbuf *v = (hal_vbuf *)self;
  if (bmp == NULL)
    return;

  const int px = (int)lroundf(x);
  const int py = (int)lroundf(y);
  const int pw = (int)lroundf(w);
  const int ph = (int)lroundf(h);
  const int bytes_per_row = (pw + 7) / 8;

  for (int row = 0; row < ph; ++row)
    for (int col = 0; col < pw; ++col)
    {
      const uint8_t byte = bmp[row * bytes_per_row + col / 8];
      if ((byte & (0x80u >> (col % 8))) != 0)
        plot(v, px + col, py + row);
    }
}

static void vbuf_draw_box(hal *self, float x, float y, float w, float h)
{
  fill_rounded((hal_vbuf *)self, (int)lroundf(x), (int)lroundf(y),
               (int)lroundf(w), (int)lroundf(h), 0.0f);
}

static void vbuf_draw_rbox(hal *self, float x, float y, float w, float h,
                           float r)
{
  fill_rounded((hal_vbuf *)self, (int)lroundf(x), (int)lroundf(y),
               (int)lroundf(w), (int)lroundf(h), r);
}

static void vbuf_draw_frame(hal *self, float x, float y, float w, float h)
{
  stroke_rounded((hal_vbuf *)self, (int)lroundf(x), (int)lroundf(y),
                 (int)lroundf(w), (int)lroundf(h), 0.0f);
}

static void vbuf_draw_rframe(hal *self, float x, float y, float w, float h,
                             float r)
{
  stroke_rounded((hal_vbuf *)self, (int)lroundf(x), (int)lroundf(y),
                 (int)lroundf(w), (int)lroundf(h), r);
}

/* ------------------------------------------------------------------------ */
/* ops: timers, peripherals, keys                                            */
/* ------------------------------------------------------------------------ */

/* Virtual time: nothing blocks. The host advances it (see hal_vbuf_advance)
   so a headless run finishes as fast as the CPU allows. */
static void vbuf_delay(hal *self, unsigned long ms)
{
  ((hal_vbuf *)self)->tick_ms += (unsigned long)ms;
}

static unsigned long vbuf_millis(hal *self)
{
  return ((hal_vbuf *)self)->tick_ms;
}

static unsigned long vbuf_get_tick(hal *self) { return vbuf_millis(self); }

static unsigned long vbuf_get_random_seed(hal *self)
{
  hal_vbuf *v = (hal_vbuf *)self;
  /* xorshift32 seeded from the clock; advances on every call so the logo stars
     move between runs. */
  v->rng ^= v->rng << 13;
  v->rng ^= v->rng >> 17;
  v->rng ^= v->rng << 5;
  return (unsigned long)(v->rng ^ v->tick_ms);
}

static void vbuf_beep(hal *self, float freq)
{
  (void)self;
  (void)freq;
}

static void vbuf_beep_stop(hal *self) { (void)self; }

static void vbuf_set_beep_vol(hal *self, uint8_t vol)
{
  (void)self;
  (void)vol;
}

static void vbuf_screen_on(hal *self) { ((hal_vbuf *)self)->display_on = true; }

static void vbuf_screen_off(hal *self)
{
  hal_vbuf *v = (hal_vbuf *)self;
  v->display_on = false;
  if (v->buffer != NULL)
    memset(v->buffer, 0, v->buffer_bytes);
}

static void vbuf_print_info(hal *self, const char *msg)
{
  (void)self;
  (void)msg;
#if ASTRA_VBUF_USE_PRINTF
  if (msg != NULL)
    printf("[astra/vbuf] %s\n", msg);
#endif
}

static void vbuf_update_config(hal *self) { (void)self; }

/* ------------------------------------------------------------------------ */
/* ops table                                                                 */
/* ------------------------------------------------------------------------ */

static const hal_ops vbuf_ops = {
    .type = vbuf_type,
    .init = vbuf_init,
    .deinit = vbuf_deinit,

    .get_canvas_buffer = vbuf_get_canvas_buffer,
    .get_buffer_tile_width = vbuf_get_buffer_tile_width,
    .get_buffer_tile_height = vbuf_get_buffer_tile_height,
    .canvas_update = vbuf_canvas_update,
    .canvas_clear = vbuf_canvas_clear,

    .set_font = vbuf_set_font,
    .get_font_width = vbuf_get_font_width,
    .get_font_height = vbuf_get_font_height,
    .set_draw_type = vbuf_set_draw_type,

    .draw_pixel = vbuf_draw_pixel,
    .draw_english = vbuf_draw_english,
    .draw_chinese = vbuf_draw_chinese,
    .draw_v_dotted_line = vbuf_draw_v_dotted_line,
    .draw_h_dotted_line = vbuf_draw_h_dotted_line,
    .draw_v_line = vbuf_draw_v_line,
    .draw_h_line = vbuf_draw_h_line,
    .draw_bmp = vbuf_draw_bmp,
    .draw_box = vbuf_draw_box,
    .draw_rbox = vbuf_draw_rbox,
    .draw_frame = vbuf_draw_frame,
    .draw_rframe = vbuf_draw_rframe,

    .delay_ms = vbuf_delay,
    .millis = vbuf_millis,
    .get_tick = vbuf_get_tick,
    .get_random_seed = vbuf_get_random_seed,

    .beep = vbuf_beep,
    .beep_stop = vbuf_beep_stop,
    .set_beep_vol = vbuf_set_beep_vol,
    .screen_on = vbuf_screen_on,
    .screen_off = vbuf_screen_off,

    .print_info = vbuf_print_info,
    .update_config = vbuf_update_config,
};

/* ------------------------------------------------------------------------ */
/* construction                                                              */
/* ------------------------------------------------------------------------ */

/* Fill in geometry and buffer bookkeeping. `canvas` may be NULL for the heap
   constructor, which allocates it afterwards. */
static bool vbuf_setup(hal_vbuf *v, const hal_vbuf_config *cfg_in,
                       uint8_t *canvas, size_t canvas_len)
{
  hal_vbuf_config cfg;
  if (cfg_in == NULL)
    config_default(&cfg);
  else
    cfg = *cfg_in;

  /* 0 means "unset, use the default"; negative stays an error. */
  if (cfg.width == 0)
    cfg.width = 128;
  if (cfg.height == 0)
    cfg.height = 64;
  if (!config_is_valid(&cfg))
    return false;

  const size_t bytes = canvas_bytes(cfg.width, cfg.height);
  if (canvas != NULL && canvas_len < bytes)
    return false;

  memset(v, 0, sizeof(*v));
  v->width = cfg.width;
  v->height = cfg.height;
  v->tile_width = cfg.width / 8;
  v->tile_height = cfg.height / 8;
  v->buffer = canvas;
  v->buffer_bytes = bytes;
  v->owns_buffer = false;
  v->draw_type = 1;
  v->display_on = true;
  v->font = &hal_vbuf_font_main;
  v->rng = 0x9E3779B9u;
  v->tick_ms = 0;
  return true;
}

hal *hal_vbuf_init_static(hal_vbuf *storage, const hal_vbuf_config *cfg,
                          uint8_t *canvas, size_t canvas_bytes_len)
{
  if (storage == NULL || canvas == NULL)
    return NULL;
  if (!vbuf_setup(storage, cfg, canvas, canvas_bytes_len))
    return NULL;

  hal_construct(&storage->base, &vbuf_ops);
  return &storage->base;
}
hal *hal_vbuf_create(const hal_vbuf_config *cfg_in)
{

  hal_vbuf_config cfg;

  if (cfg_in == NULL)
    config_default(&cfg);
  else
    cfg = *cfg_in;
  /* 0 means "unset, use the default"; negative stays an error. */
  if (cfg.width == 0)
    cfg.width = 128;
  if (cfg.height == 0)
    cfg.height = 64;
  if (!config_is_valid(&cfg))
    return NULL;
  hal_vbuf *v = (hal_vbuf *)calloc(1, sizeof(hal_vbuf));
  if (v == NULL)
    return NULL;
  uint8_t *canvas = (uint8_t *)calloc(canvas_bytes(cfg.width, cfg.height), 1);
  if (canvas == NULL)
  {
    free(v);
    return NULL;
  }

  if (!vbuf_setup(v, &cfg, canvas, canvas_bytes(cfg.width, cfg.height)))
  {
    free(canvas);
    free(v);
    return NULL;
  }

  v->owns_buffer = true;
  /* Owned: hal_destroy() frees the instance, deinit() frees the canvas. */
  hal_construct_owned(&v->base, &vbuf_ops);
  return &v->base;
}

void hal_vbuf_deinit(hal_vbuf *v)
{
  if (v == NULL)
    return;
  vbuf_deinit(&v->base); /* releases the canvas only when it owns it */
}

/* ------------------------------------------------------------------------ */
/* host facing API                                                           */
/* ------------------------------------------------------------------------ */

static hal_vbuf *current(void)
{
  hal *h = hal_get();
  if (h == NULL || h->ops != &vbuf_ops)
    return NULL;
  return (hal_vbuf *)h;
}

const uint8_t *hal_vbuf_canvas(void)
{
  hal_vbuf *v = current();
  return v == NULL ? NULL : v->buffer;
}

int hal_vbuf_width(void)
{
  hal_vbuf *v = current();
  return v == NULL ? 0 : v->width;
}

int hal_vbuf_height(void)
{
  hal_vbuf *v = current();
  return v == NULL ? 0 : v->height;
}

void hal_vbuf_set_flush(hal_vbuf_flush_fn fn, void *user)
{
  hal_vbuf *v = current();
  if (v == NULL)
    return;
  v->flush = fn;
  v->flush_user = user;
}

void hal_vbuf_advance(unsigned long ms)
{
  hal_vbuf *v = current();
  if (v == NULL)
    return;
  v->tick_ms += ms;
}
