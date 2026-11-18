/*
 * ETCH CRANK -- by Synth Centric
 * The 1-bit renderer (see gfx1.h).  Portable C: the device and the host
 * tests use this file as it is.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gfx1.h"

uint8_t gfx_fb[GFX_H * GFX_STRIDE];
uint8_t gfx_dirty[GFX_H];

extern const uint8_t font_small_w[64], font_small_rows[320];
extern const uint8_t font_main_w[64], font_main_rows[448];

static const uint8_t dith[3][2] = {
    { 0x22, 0x88 },          /* C_D75: 75 % black (1 = white)       */
    { 0x55, 0xAA },          /* C_D50                               */
    { 0xDD, 0x77 },          /* C_D25: 25 % black                   */
};

static inline void plot(int x, int y, int c)
{
    uint8_t *p, m;
    if ((unsigned)x >= GFX_W || (unsigned)y >= GFX_H) return;
    p = &gfx_fb[y * GFX_STRIDE + (x >> 3)];
    m = (uint8_t)(0x80 >> (x & 7));
    switch (c) {
    case C_BLACK: *p &= (uint8_t)~m; break;
    case C_WHITE: *p |= m; break;
    case C_XOR: *p ^= m; break;
    default: {
        uint8_t pat = dith[c - C_D75][y & 1];
        if (pat & m) *p |= m; else *p &= (uint8_t)~m;
    }
    }
    gfx_dirty[y] = 1;
}

void gfx_pixel(int x, int y, int c) { plot(x, y, c); }

void gfx_clear(int c)
{
    gfx_fill(0, 0, GFX_W, GFX_H, c);
}

void gfx_fill(int x, int y, int w, int h, int c)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    if (w <= 0 || h <= 0) return;
    if ((c == C_BLACK || c == C_WHITE) && !(x & 7) && !(w & 7)) {
        for (j = y; j < y + h; j++) {
            memset(&gfx_fb[j * GFX_STRIDE + (x >> 3)], c == C_WHITE ? 0xFF : 0x00, (size_t)(w >> 3));
            gfx_dirty[j] = 1;
        }
        return;
    }
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) plot(i, j, c);
}

void gfx_hline(int x, int y, int w, int c) { gfx_fill(x, y, w, 1, c); }
void gfx_vline(int x, int y, int h, int c) { gfx_fill(x, y, 1, h, c); }

void gfx_frame(int x, int y, int w, int h, int c)
{
    gfx_hline(x, y, w, c);
    gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y + 1, h - 2, c);
    gfx_vline(x + w - 1, y + 1, h - 2, c);
}

void gfx_line(int x0, int y0, int x1, int y1, int c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

void gfx_arc(int cx, int cy, int r, int thick, float a0, float a1, int c)
{
    int x, y;
    float r2o = (float)r * r, r2i = (float)(r - thick) * (r - thick);
    float span = a1 - a0;
    if (span <= 0) return;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            float d2 = (float)(x * x + y * y), a;
            if (d2 > r2o || d2 < r2i) continue;
            a = atan2f((float)x, (float)-y) * 57.29578f;      /* 0 = up, clockwise */
            if (a < 0) a += 360.0f;
            a -= a0;
            while (a < 0) a += 360.0f;
            if (a <= span) plot(cx + x, cy + y, c);
        }
}

/* ---- text ----------------------------------------------------------------- */
static inline int glyph(int font, char ch, const uint8_t **rows)
{
    int i;
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch < 32 || ch > 95) ch = '?';
    i = ch - 32;
    if (font == F_SMALL) { *rows = font_small_rows + i * 5; return font_small_w[i]; }
    *rows = font_main_rows + i * 7;
    return font_main_w[i];
}

int gfx_font_h(int font) { return font == F_SMALL ? 5 : 7; }

int gfx_text_w(int font, int scale, const char *s)
{
    int w = 0;
    const uint8_t *r;
    for (; *s; s++) w += (glyph(font, *s, &r) + 1) * scale;
    return w ? w - scale : 0;
}

int gfx_text(int x, int y, int font, int scale, int c, const char *s)
{
    int h = gfx_font_h(font);
    for (; *s; s++) {
        const uint8_t *rows;
        int w = glyph(font, *s, &rows), i, j;
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                if (rows[j] & (0x80 >> i)) {
                    if (scale == 1) plot(x + i, y + j, c);
                    else gfx_fill(x + i * scale, y + j * scale, scale, scale, c);
                }
        x += (w + 1) * scale;
    }
    return x;
}

void gfx_text_r(int rx, int y, int font, int scale, int c, const char *s)
{
    gfx_text(rx - gfx_text_w(font, scale, s), y, font, scale, c, s);
}

void gfx_text_c(int cx, int y, int font, int scale, int c, const char *s)
{
    gfx_text(cx - gfx_text_w(font, scale, s) / 2, y, font, scale, c, s);
}

int gfx_textf(int x, int y, int font, int scale, int c, const char *fmt, ...)
{
    char b[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    return gfx_text(x, y, font, scale, c, b);
}

int gfx_wrap(int x, int y, int w, int font, int line_h, int c, const char *s)
{
    char line[192], word[64];
    int ln = 0;
    line[0] = 0;
    while (*s) {
        int n = 0;
        if (*s == ' ') { s++; continue; }
        if (*s == '\n') {
            if (ln) { gfx_text(x, y, font, 1, c, line); y += line_h; line[0] = 0; ln = 0; }
            y += line_h / 2;
            s++;
            continue;
        }
        while (*s && *s != ' ' && *s != '\n' && n < 63) word[n++] = *s++;
        word[n] = 0;
        if (ln) {
            char test[192];
            snprintf(test, sizeof test, "%s %s", line, word);
            if (gfx_text_w(font, 1, test) > w) {
                gfx_text(x, y, font, 1, c, line);
                y += line_h;
                snprintf(line, sizeof line, "%s", word);
            } else snprintf(line, sizeof line, "%s", test);
        } else snprintf(line, sizeof line, "%s", word);
        ln = (int)strlen(line);
    }
    if (ln) { gfx_text(x, y, font, 1, c, line); y += line_h; }
    return y;
}
