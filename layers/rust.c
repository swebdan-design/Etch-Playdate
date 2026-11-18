/*
 * ETCH CRANK -- by Synth Centric
 * RUST: the second voice (an ETCH DS layer).  Integer multiply/shift/add
 * only, no division, no floating point; runs in the audio callback.
 *
 *   oscillators  per note, A and B: ACID's band-limited wavetables (SAW,
 *                SQUARE, HOLLOW, BITE; one mip level per octave), B an
 *                interval above A and detuned.  Hard sync: A's wrap resets B.
 *                Ring: blends A + B toward A x B.
 *   paraphony    MONO: one note.  PARA3: up to three notes share one filter
 *                and one amplitude envelope (a stab).
 *   filter       a 2-pole resonant low-pass (two of ACID's one-pole stages
 *                with feedback), coefficient from ACID's cut_g table, swept
 *                by the envelope
 *   envelope     instant attack, exponential decay (ACID's dec_coef), a
 *                quick release at the gate
 */
#include "layers.h"
#include "dsp_tables.h"

rust_params_t rust_p = { RW_SAW, RW_SQUARE, 7, 20, 0, 0, 52, 70, 60, 50, RM_MONO, 70, 9, { 0 } };

typedef struct {
    uint32_t pa, pb, ia, ib;
    uint8_t  on, oa, ob;          /* octaves: the wavetable mip levels */
} rvoice_t;

static rvoice_t rv[3];
static uint8_t nvoices, held, accented, active;
static int32_t env, amp, y1, y2;           /* env Q16, amp Q15 */
static int32_t dcx, dcy;

/* pitch (1/64 semitone, 0 = C1) -> phase increment and octave, as ACID */
static inline uint32_t pinc(int32_t p, uint32_t *oct)
{
    int32_t o, r;
    if (p < 0) p = 0;
    if (p > 8 * 768 + 767) p = 8 * 768 + 767;
    o = (p * 21846) >> 24;
    r = p - o * 768;
    if (r >= 768) { r -= 768; o++; }
    if (r < 0) { r += 768; o--; }
    *oct = (uint32_t)o;
    return inc_top[r] >> (8 - o);
}

void rust_init(void)
{
    uint8_t i;
    for (i = 0; i < 3; i++) rv[i].on = 0;
    nvoices = held = accented = active = 0;
    env = amp = y1 = y2 = dcx = dcy = 0;
}

uint8_t rust_busy(void) { return active; }

void rust_note(const uint8_t *notes, uint8_t n, uint8_t accent, uint8_t glide)
{
    uint8_t i;
    uint32_t oct;
    if (n > 3) n = 3;
    for (i = 0; i < 3; i++) {
        rvoice_t *v = &rv[i];
        if (i < n) {
            int32_t pa = (int32_t)notes[i] << 6;
            int32_t pb = pa + ((int32_t)rust_p.interval << 6) + (rust_p.detune >> 2);
            v->ia = pinc(pa, &oct);
            v->oa = (uint8_t)(oct > WT_MIPS - 1 ? WT_MIPS - 1 : oct);
            v->ib = pinc(pb, &oct);
            v->ob = (uint8_t)(oct > WT_MIPS - 1 ? WT_MIPS - 1 : oct);
            if (!v->on || !glide) { v->pa = 0; v->pb = 0; }
            v->on = 1;
        } else v->on = 0;
    }
    nvoices = n;
    accented = accent;
    if (!glide || !held) env = 65535;
    held = 1;
    active = 1;
}

void rust_gate_off(void) { held = 0; }

static inline int32_t sat(int32_t u)
{
    if (u > 24576) u = 24576;
    if (u < -24576) u = -24576;
    {
        int32_t u2 = (u * u) >> 14, u3 = (u2 * u) >> 14;
        return u - ((u3 * 2427) >> 14);
    }
}

void rust_render(int32_t *L, int32_t *R, uint32_t n)
{
    uint32_t i, k;
    int32_t cp, g, kres, amp_t, amp_new, damp, ring = rust_p.ring & 127;
    const int16_t *ta[3], *tb[3];
    int32_t gl = (rust_p.level & 127) * (accented ? 160 : 128);   /* Q14 */
    if (!active) return;

    /* control tick */
    env = (int32_t)(((uint32_t)env * dec_coef[rust_p.decay & 127]) >> 16);
    amp_t = held ? (accented ? 32767 : 26000) : 0;
    if (!held && amp < 24) { active = 0; amp = 0; return; }
    amp_new = amp + ((amp_t - amp) >> (held ? 2 : 3));  /* ~4 ms / ~8 ms per tick */
    damp = (amp_new - amp) >> 5;                         /* across the 32 samples */
    cp = 470 + (int32_t)(rust_p.cutoff & 127) * 42 + ((env * ((int32_t)(rust_p.envmod & 127) * 30)) >> 16);
    if (accented) cp += 400;
    {
        int32_t idx = (cp >> 4) + 48;
        if (idx < 0) idx = 0;
        if (idx > 511) idx = 511;
        g = cut_g[idx];
    }
    kres = res_k[rust_p.res & 127] >> 1;                 /* a 2-pole needs half */
    for (k = 0; k < 3; k++) {
        if (!rv[k].on) continue;
        ta[k] = wt[rust_p.wave_a & 3][rv[k].oa];
        tb[k] = wt[rust_p.wave_b & 3][rv[k].ob];
    }

    for (i = 0; i < n; i++) {
        int32_t s = 0, x, out;
        for (k = 0; k < 3; k++) {
            rvoice_t *v = &rv[k];
            int32_t a, b;
            uint32_t old;
            if (!v->on) continue;
            a = ta[k][v->pa >> 22];
            b = tb[k][v->pb >> 22];
            old = v->pa;
            v->pa += v->ia;
            v->pb += v->ib;
            if (rust_p.sync && v->pa < old) v->pb = v->pa;           /* hard sync */
            /* ring: from (a + b) / 2 toward a * b */
            x = ((a + b) >> 1) * (128 - ring) + ((a * b) >> 15) * ring;
            s += x >> 7;
        }
        if (nvoices > 1) s = (s * 21845) >> 16;          /* three notes: a third each */
        s >>= 1;                                         /* Q15 -> Q14 */
        /* 2-pole resonant low-pass */
        {
            int32_t u = sat(s - ((kres * y2) >> 12));
            y1 += ((u - y1) * g) >> 16;
            y2 += ((y1 - y2) * g) >> 16;
        }
        out = (y2 * amp) >> 15;
        amp += damp;
        /* DC blocker */
        {
            int32_t yv = out - dcx + ((dcy * 32604) >> 15);
            dcx = out;
            dcy = yv;
            out = yv;
        }
        out = (out * gl) >> 14;
        if (out > 32767) out = 32767;
        if (out < -32768) out = -32768;
        L[i] += (out * 26000) >> 15;                     /* a little right of centre */
        R[i] += (out * 30000) >> 15;
    }
    amp = amp_new;
}
