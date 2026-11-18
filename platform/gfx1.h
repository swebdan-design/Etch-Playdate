/*
 * ETCH CRANK -- by Synth Centric
 * The 1-bit renderer: every screen is drawn into one 400 x 240 buffer in the
 * Playdate's own frame layout (52 bytes a row, MSB first, 1 = white), with
 * ETCH DS's two pixel fonts (platform/font_ui.c, the series' SMALL 3x5 and
 * MAIN 5x7).  Rows that changed are marked; on the device gfx_present()
 * copies only those into the display frame.  The host tests draw the same
 * buffer and write it out as an image.
 *
 * Colour roles (the series' roles mapped onto 1 bit):
 *   C_BLACK  TEXT, HOT, NOTE      C_WHITE  BG, SURF
 *   C_D75 / C_D50 / C_D25  dither (75 / 50 / 25 % black): ACC_TINT, fades,
 *                          disabled controls, beat columns
 *   C_XOR    inverts (the cursor, lit pads)
 */
#ifndef ETCH_CRANK_GFX1_H
#define ETCH_CRANK_GFX1_H
#include <stdint.h>

#define GFX_W      400
#define GFX_H      240
#define GFX_STRIDE 52

enum { C_BLACK, C_WHITE, C_D75, C_D50, C_D25, C_XOR };
enum { F_SMALL, F_MAIN };

extern uint8_t gfx_fb[GFX_H * GFX_STRIDE];
extern uint8_t gfx_dirty[GFX_H];

void gfx_clear(int c);
void gfx_fill(int x, int y, int w, int h, int c);
void gfx_frame(int x, int y, int w, int h, int c);            /* 1 px outline */
void gfx_hline(int x, int y, int w, int c);
void gfx_vline(int x, int y, int h, int c);
void gfx_line(int x0, int y0, int x1, int y1, int c);
void gfx_pixel(int x, int y, int c);
/* an arc of a ring: centre, outer radius, thickness, from a0 to a1 degrees
 * (0 = up, clockwise; a1 may exceed 360) */
void gfx_arc(int cx, int cy, int r, int thick, float a0, float a1, int c);

int  gfx_text_w(int font, int scale, const char *s);
int  gfx_text(int x, int y, int font, int scale, int c, const char *s);   /* -> x after */
void gfx_text_r(int rx, int y, int font, int scale, int c, const char *s);
void gfx_text_c(int cx, int y, int font, int scale, int c, const char *s);
int  gfx_textf(int x, int y, int font, int scale, int c, const char *fmt, ...);
int  gfx_font_h(int font);
/* word-wrapped text; returns the y after the last line */
int  gfx_wrap(int x, int y, int w, int font, int line_h, int c, const char *s);

#endif
