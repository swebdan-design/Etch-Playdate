/*
 * ETCH CRANK -- by Synth Centric
 * The audio pump (see pump.h).
 */
#include <string.h>
#include "pump.h"
#include "seq_core.h"
#include "audio.h"
#include "resample.h"
#include "crank.h"

static shared_t *psh;
static uint32_t blk;                      /* next engine block to render   */
void (*pump_tap)(const int16_t *l, const int16_t *r, uint32_t blk);
volatile pump_stats_t pstats;
volatile uint32_t pump_cmd_drops;
void (*pump_source)(int16_t *l, int16_t *r);

/* ---- command ring: update loop -> audio callback -------------------------
 * Single producer (the update loop), single consumer (the callback).  On the
 * device the callback interrupts the update loop on the one core; in the
 * Simulator it is another thread, hence the barriers. */
static volatile uint32_t cmdq[PUMP_CMDQ];
static volatile uint32_t cmd_head, cmd_tail;

#define BARRIER() __sync_synchronize()

/* ETCH DS's app_send: the command reaches the sequencer before the next
 * block is sequenced, as a FIFO command reached the ARM7 between blocks.
 * The ring never fills in practice (a frame sends a handful); if it ever
 * did, the command is dropped and counted rather than waited for. */
void app_send(uint32_t cmd)
{
    uint32_t h = cmd_head;
    if (h - cmd_tail >= PUMP_CMDQ) { pump_cmd_drops++; return; }
    cmdq[h & (PUMP_CMDQ - 1)] = cmd;
    BARRIER();                              /* SH writes (edit_pat...) first */
    cmd_head = h + 1;
}

/* ---- engine-rate ring for the resampler -----------------------------------
 * Mirrored: every frame is written at i and i + PUMP_RING, so the 16-tap
 * window starting anywhere in the first half is contiguous in memory. */
static int16_t ring_l[2 * PUMP_RING], ring_r[2 * PUMP_RING];
static uint32_t ring_w;                   /* engine frames written          */
static uint32_t rs_i;                     /* resampler position: integer    */
static uint32_t rs_f;                     /*   and 2^-32 fraction           */

static inline uint32_t clk(void) { return host_clock ? host_clock() : 0; }
static inline uint16_t pct(uint32_t sum) { return (uint16_t)(((uint64_t)sum * 10000u) >> 25); }

void pump_init(shared_t *sh)
{
    psh = sh;
    blk = 0;
    cmd_head = cmd_tail = 0;
    memset(ring_l, 0, sizeof ring_l);
    memset(ring_r, 0, sizeof ring_r);
    ring_w = 0;
    rs_i = RS_TAPS / 2 - 1;               /* the first output sits on frame 7 */
    rs_f = 0;
    crank_audio_reset();
    seq_init(sh);
}

uint32_t pump_blocks(void) { return blk; }

static void drain_commands(void)
{
    while (cmd_tail != cmd_head) {
        uint32_t c;
        BARRIER();
        c = cmdq[cmd_tail & (PUMP_CMDQ - 1)];
        if ((c >> 24) >= 0xC0) crank_command(c);
        else seq_command(c);
        cmd_tail++;
    }
}

/* one engine block: commands, sequencer, engine.  l/r receive it. */
static void render_block(int16_t *l, int16_t *r)
{
    uint32_t k = blk, t;
    t = clk();
    drain_commands();
    seq_render_block(k);
    psh->blk_requested = k + 1;
    psh->blk_played = k;
    pstats.seq_sum += clk() - t;
    audio_render_block(&psh->ev[k & (AUDIO_NBLK - 1)], k, l, r);
    psh->blk_done = k + 1;
    if (pump_tap) pump_tap(l, r, k);
    blk = k + 1;
}

void pump_block(int16_t *l, int16_t *r)
{
    render_block(l, r);
}

static void ring_block(void)
{
    int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK];
    uint32_t i, w = ring_w;
    if (pump_source) { pump_source(l, r); blk++; }
    else render_block(l, r);
    for (i = 0; i < AUDIO_BLOCK; i++) {
        uint32_t j = (w + i) & (PUMP_RING - 1);
        ring_l[j] = ring_l[j + PUMP_RING] = l[i];
        ring_r[j] = ring_r[j + PUMP_RING] = r[i];
    }
    ring_w = w + AUDIO_BLOCK;
}

static inline int16_t sat16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

/* two 16-bit products and an accumulate: the Cortex-M7's SMLAD, or the same
 * integer arithmetic in C (exact: the sums never leave int32, see resample.h) */
static inline uint32_t ld2(const int16_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline int32_t smlad(uint32_t a, uint32_t b, int32_t acc)
{
#if defined(__ARM_FEATURE_DSP)
    int32_t r;
    __asm__("smlad %0, %1, %2, %3" : "=r"(r) : "r"(a), "r"(b), "r"(acc));
    return r;
#else
    return acc + (int32_t)(int16_t)a * (int16_t)b + (int32_t)(int16_t)(a >> 16) * (int16_t)(b >> 16);
#endif
}

/* the filter at one phase, both channels, rounded and saturated to int16 */
static inline void dot(const int16_t *xl, const int16_t *xr, const int16_t *c, int32_t *yl, int32_t *yr)
{
    int32_t al = 16384, ar = 16384;
    uint32_t k;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC unroll 8
#endif
    for (k = 0; k < RS_TAPS; k += 2) {
        uint32_t cc = ld2(c + k);
        al = smlad(ld2(xl + k), cc, al);
        ar = smlad(ld2(xr + k), cc, ar);
    }
    *yl = sat16(al >> 15);
    *yr = sat16(ar >> 15);
}

void pump_pull(int16_t *left, int16_t *right, uint32_t len)
{
    uint32_t n, t0 = clk(), trs = 0, ta;
    uint32_t i = rs_i, f = rs_f, w = ring_w;
    for (n = 0; n < len; n++) {
        int32_t l0, r0, l1, r1, fr;
        uint32_t ph, base;
        /* the window needs frames i - 7 .. i + 8 */
        while (w <= i + RS_TAPS / 2) {
            trs += clk() - t0;              /* the block is charged to the engine */
            ring_block();
            w = ring_w;
            t0 = clk();
        }
        ph = f >> (32 - RS_PHASE_BITS);
        fr = (int32_t)((f >> (32 - RS_PHASE_BITS - 15)) & 0x7FFF);
        base = (i - (RS_TAPS / 2 - 1)) & (PUMP_RING - 1);
        /* the two neighbouring phases, then linear between them; both ends
         * are int16, so (l1 - l0) * fr stays inside int32 */
        dot(ring_l + base, ring_r + base, rs_coef[ph], &l0, &r0);
        dot(ring_l + base, ring_r + base, rs_coef[ph + 1], &l1, &r1);
        left[n] = (int16_t)(l0 + (((l1 - l0) * fr + 16384) >> 15));
        right[n] = (int16_t)(r0 + (((r1 - r0) * fr + 16384) >> 15));
        {
            uint32_t g = f + rs_step_frac;
            i += (g < f);                   /* carry: one input frame on */
            f = g;
        }
    }
    rs_i = i;
    rs_f = f;
    ta = clk();
    trs += ta - t0;
    pstats.rs_sum += trs;
    pstats.frames += len;
    pstats.callbacks++;
    /* the callback's own total is measured by the caller (main.c) */
    if (blk - pstats.blocks >= 256u) {
        pstats.blocks = blk;
        pstats.rs_pct = pct(pstats.rs_sum);  pstats.rs_sum = 0;
        pstats.seq_pct = pct(pstats.seq_sum); pstats.seq_sum = 0;
        pstats.cb_sum_last = pstats.cb_sum;  pstats.cb_sum = 0;
        pstats.cb_max_last = pstats.cb_max;  pstats.cb_max = 0;
        pstats.frames_last = pstats.frames;  pstats.frames = 0;
        pstats.windows++;
    }
}

uint32_t pump_scope(int16_t *out, uint32_t n)
{
    uint32_t w = ring_w, i, start, trig = 0;   /* reads the first half only */
    if (w < 3 * AUDIO_BLOCK) return 0;
    if (n > 2 * AUDIO_BLOCK) n = 2 * AUDIO_BLOCK;
    start = w - 3 * AUDIO_BLOCK;
    for (i = 1; i < AUDIO_BLOCK; i++) {
        int32_t a = ring_l[(start + i - 1) & (PUMP_RING - 1)];
        int32_t b = ring_l[(start + i) & (PUMP_RING - 1)];
        if (a < 0 && b >= 0) { trig = i; break; }
    }
    for (i = 0; i < n; i++) out[i] = ring_l[(start + trig + i) & (PUMP_RING - 1)];
    return n;
}
