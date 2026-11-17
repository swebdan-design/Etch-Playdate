/*
 * ETCH DS -- by Synth Centric
 * Master FX chain: drive -> bitcrush / sample-rate reduction -> tempo-synced
 * degrading delay -> short metallic comb resonator -> limiter.
 *
 * Stereo int32 bus at Q15 scale in, int16 out.  Every stage is integer
 * multiply/shift/add; the limiter's gain comes from a reciprocal table.
 *
 *   drive   gain 1..5 into a cubic soft clip (0 = bypass)
 *   crush   sample-and-hold every N samples, then mask to B bits
 *   delay   mono send, 1..8 sixteenths (tempo-synced through a table of
 *           samples per 16th by BPM).  The feedback path darkens (one-pole
 *           low-pass) and loses bits on every repeat (dly_rot), so echoes
 *           corrode.  The right channel reads 11 ms later for width.
 *   comb    feedback comb tuned to a note: short metallic ringing on the
 *           mono sum, mixed back in.
 *   limit   look-ahead of one control tick (32 samples): the tick's peak sets
 *           the target gain (ceiling / peak) and a lower gain applies to the
 *           whole tick, so nothing passes the ceiling; release ramps, ~60 ms.
 */
#include "audio.h"
#include "dsp_tables.h"

fx_params_t fx_p = {
    24,      /* drive: on, light */
    16, 1,   /* crush off        */
    3, 56, 30, 40,   /* delay: dotted 8th, low mix, corroding */
    21, 96, 18,      /* comb: A2-ish ring, low mix */
    1,       /* limiter on       */
    124
};

#define DLY_LEN  32768u                 /* 1 s: 4 sixteenths at 60 BPM, 8 above 120 */
#define DLY_MASK (DLY_LEN - 1)
#define COMB_LEN 1024u
#define COMB_MASK (COMB_LEN - 1)

static int16_t dly_buf[DLY_LEN];
static uint32_t dly_w;
static int32_t dly_lp;
static int16_t comb_buf[COMB_LEN] AUDIO_BSS;
static uint32_t comb_w;
static int32_t crush_cnt, crush_l, crush_r;
static int32_t lim_gain = 32767;

void fx_init(void)
{
    uint32_t i;
    for (i = 0; i < DLY_LEN; i++) dly_buf[i] = 0;
    for (i = 0; i < COMB_LEN; i++) comb_buf[i] = 0;
    dly_w = comb_w = 0;
    dly_lp = 0;
    crush_cnt = 0;
    lim_gain = 32767;
}

/* cubic soft clip, Q15 in/out: y = x - 4/27 x^3 for |x| <= 1.5, +-1 beyond
 * (evaluated in Q14 so every product fits in 32 bits) */
static inline int32_t clip_cubic15(int32_t x)
{
    int32_t u = x >> 1, u2, u3;
    if (u > 24576) u = 24576;
    if (u < -24576) u = -24576;
    u2 = (u * u) >> 14;
    u3 = (u2 * u) >> 14;
    return (u - ((u3 * 2427) >> 14)) << 1;
}

AUDIO_CODE void fx_drive(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    int32_t g = 256 + (int32_t)fx_p.drive * 8;     /* Q8: 1 .. 5 */
    if (!fx_p.drive) return;
    for (i = 0; i < n; i++) {
        l[i] = (clip_cubic15((l[i] * g) >> 8) * 24576) >> 15;     /* peak 0.75 */
        r[i] = (clip_cubic15((r[i] * g) >> 8) * 24576) >> 15;
    }
}

AUDIO_CODE void fx_crush(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, hold = fx_p.crush_hold, bits = fx_p.crush_bits;
    int32_t mask;
    if (hold <= 1 && bits >= 16) return;
    if (bits < 2) bits = 2;
    if (bits > 16) bits = 16;
    mask = ~((1 << (16 - bits)) - 1);
    for (i = 0; i < n; i++) {
        if (hold > 1) {
            if (crush_cnt <= 0) { crush_l = l[i]; crush_r = r[i]; crush_cnt = (int32_t)hold; }
            crush_cnt--;
            l[i] = crush_l;
            r[i] = crush_r;
        }
        l[i] &= mask;
        r[i] &= mask;
    }
}

AUDIO_CODE void fx_delay(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, steps = fx_p.dly_steps, len;
    int32_t fb = (int32_t)fx_p.dly_fb << 8, mix = (int32_t)fx_p.dly_mix << 8;
    int32_t rot = fx_p.dly_rot, lpg, rmask;
    if (!fx_p.dly_mix) return;
    if (steps < 1) steps = 1;
    if (steps > 8) steps = 8;
    len = (uint32_t)step_len[fx_p.bpm] * steps;
    if (len > DLY_LEN - 400) len = DLY_LEN - 400;
    lpg = 65535 - rot * 400;                       /* darker per repeat */
    rmask = ~((1 << (rot >> 4)) - 1);              /* and fewer bits    */
    for (i = 0; i < n; i++) {
        int32_t dl = dly_buf[(dly_w - len) & DLY_MASK];
        int32_t dr = dly_buf[(dly_w - len - 360) & DLY_MASK];
        int32_t in = (l[i] + r[i]) >> 2, w;
        dly_lp += ((dl - dly_lp) * lpg) >> 16;
        w = in + (((dly_lp & rmask) * fb) >> 15);
        if (w > 32767) w = 32767;
        if (w < -32767) w = -32767;
        dly_buf[dly_w & DLY_MASK] = (int16_t)w;
        dly_w++;
        l[i] += (dl * mix) >> 15;
        r[i] += (dr * mix) >> 15;
    }
}

AUDIO_CODE void fx_comb(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, len;
    int32_t fb = (int32_t)fx_p.comb_fb << 8, mix = (int32_t)fx_p.comb_mix << 8;
    if (!fx_p.comb_mix) return;
    len = note_period[fx_p.comb_note & 127] >> 8;
    if (len < 8) len = 8;
    if (len > COMB_LEN - 1) len = COMB_LEN - 1;
    for (i = 0; i < n; i++) {
        int32_t c = comb_buf[(comb_w - len) & COMB_MASK];
        int32_t w = ((l[i] + r[i]) >> 3) + ((c * fb) >> 15);
        if (w > 32767) w = 32767;
        if (w < -32767) w = -32767;
        comb_buf[comb_w & COMB_MASK] = (int16_t)w;
        comb_w++;
        c = (c * mix) >> 15;
        l[i] += c;
        r[i] += c;
    }
}

AUDIO_CODE void fx_limit(int32_t *l, int32_t *r, int16_t *ol, int16_t *or_, uint32_t n)
{
    uint32_t i, j;
    for (j = 0; j < n; j += CTRL_LEN) {
        int32_t peak = 0, target = 32767, g0 = lim_gain, dg;
        if (fx_p.limit) {
            for (i = j; i < j + CTRL_LEN; i++) {
                int32_t a = l[i] < 0 ? -l[i] : l[i];
                int32_t b = r[i] < 0 ? -r[i] : r[i];
                if (a > peak) peak = a;
                if (b > peak) peak = b;
            }
            if (peak > LIMIT_CEIL) {
                uint32_t ix = ((uint32_t)peak >> 8) + 1;             /* round up */
                target = limit_recip[ix > 1023 ? 1023 : ix];
            }
            if (target < lim_gain) { lim_gain = target; g0 = target; }  /* attack: whole tick */
            else lim_gain += (target - lim_gain) >> 6;                  /* release ~60 ms     */
        }
        dg = (lim_gain - g0) >> 5;
        for (i = j; i < j + CTRL_LEN; i++) {
            int32_t a = (int32_t)(((int64_t)l[i] * g0) >> 15);
            int32_t b = (int32_t)(((int64_t)r[i] * g0) >> 15);
            g0 += dg;
            if (a > 32767) a = 32767;
            if (a < -32767) a = -32767;
            if (b > 32767) b = 32767;
            if (b < -32767) b = -32767;
            ol[i] = (int16_t)a;
            or_[i] = (int16_t)b;
        }
    }
}
