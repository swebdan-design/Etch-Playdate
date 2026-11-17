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

/* ---- engine-rate ring for the resampler ---------------------------------- */
static int16_t ring_l[PUMP_RING], ring_r[PUMP_RING];
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
        ring_l[(w + i) & (PUMP_RING - 1)] = l[i];
        ring_r[(w + i) & (PUMP_RING - 1)] = r[i];
    }
    ring_w = w + AUDIO_BLOCK;
}

static inline int16_t sat16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

void pump_pull(int16_t *left, int16_t *right, uint32_t len)
{
    uint32_t n, k, t0 = clk(), trs = 0, ta;
    for (n = 0; n < len; n++) {
        int32_t c[RS_TAPS], al = 0, ar = 0;
        uint32_t ph, fr, base;
        const int16_t *c0, *c1;
        /* the window needs frames rs_i - 7 .. rs_i + 8 */
        while (ring_w <= rs_i + RS_TAPS / 2) {
            trs += clk() - t0;              /* the block is charged to the engine */
            ring_block();
            t0 = clk();
        }
        ph = rs_f >> (32 - RS_PHASE_BITS);
        fr = (rs_f >> (32 - RS_PHASE_BITS - 15)) & 0x7FFF;
        c0 = rs_coef[ph];
        c1 = rs_coef[ph + 1];
        for (k = 0; k < RS_TAPS; k++)
            c[k] = c0[k] + (((c1[k] - c0[k]) * (int32_t)fr + 16384) >> 15);
        base = rs_i - (RS_TAPS / 2 - 1);
        for (k = 0; k < RS_TAPS; k++) {
            uint32_t j = (base + k) & (PUMP_RING - 1);
            al += ring_l[j] * c[k];
            ar += ring_r[j] * c[k];
        }
        left[n] = sat16((al + 16384) >> 15);
        right[n] = sat16((ar + 16384) >> 15);
        {
            uint32_t f = rs_f + rs_step_frac;
            rs_i += (f < rs_f);             /* carry: one input frame on */
            rs_f = f;
        }
    }
    ta = clk();
    trs += ta - t0;
    pstats.rs_sum += trs;
    pstats.frames += len;
    pstats.callbacks++;
    /* the callback's own total is measured by the caller (pump_account) */
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
    uint32_t w = ring_w, i, start, trig = 0;
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
