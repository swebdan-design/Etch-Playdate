/*
 * ETCH DS -- by Synth Centric
 * Block renderer: turns one block's events into audio, stage by stage, and
 * measures what every stage costs.
 *
 * Runs in the IPC-sync interrupt (see main.c): the ARM7 rings once per block
 * and this renders every requested block, so the UI and CORROSION in the
 * main loop can never delay audio.  Code in ITCM, hot state in DTCM.
 *
 * Events carry a sample offset that is a multiple of SEQ_SUB, which is also
 * the control period, so each stage walks the block in CTRL_LEN chunks and
 * applies that chunk's events before rendering it: onsets are exact to the
 * sequencer's 32-sample grid and control changes land on chunk boundaries.
 *
 * CPU measurement: TIMER2+TIMER3 cascade as a free-running 32-bit counter at
 * the bus clock (33.51 MHz; the ARM9 runs at twice that).  Each stage's
 * cost is accumulated over a window of 256 blocks (1 s) and published as a
 * percentage of the block period (AUDIO_BLOCK * 1024 bus clocks = 3.91 ms).
 */
#include <string.h>
#include "audio.h"

volatile audio_stats_t astats;
const char * const stage_names[ST_COUNT] = {
    "EVENTS", "ACID", "DRUMS", "DRIVE", "CRUSH", "DELAY", "COMB", "LIMIT", "OUT"
};

static int32_t bus_l[AUDIO_BLOCK] AUDIO_BSS;
static int32_t bus_r[AUDIO_BLOCK] AUDIO_BSS;
static int32_t acid_buf[AUDIO_BLOCK] AUDIO_BSS;
static ev_t evq[EV_MAX] AUDIO_BSS;
static uint32_t nev;

#ifdef __NDS__
#include <nds.h>
static inline uint32_t clk(void)
{
    uint32_t hi, lo, hi2;
    do {
        hi = TIMER_DATA(3);
        lo = TIMER_DATA(2);
        hi2 = TIMER_DATA(3);
    } while (hi != hi2);
    return (hi << 16) | lo;
}
#else
uint32_t (*host_clock)(void);
static inline uint32_t clk(void) { return host_clock ? host_clock() : 0; }
#endif

#define BLOCK_BUS_CLOCKS (AUDIO_BLOCK * 1024u)
#define STATS_WINDOW 256u

void audio_engine_init(void)
{
    acid_init();
    fx_init();
    memset((void *)&astats, 0, sizeof astats);
#ifdef __NDS__
    TIMER_CR(2) = 0;
    TIMER_CR(3) = 0;
    TIMER_DATA(2) = 0;
    TIMER_DATA(3) = 0;
    TIMER_CR(3) = TIMER_ENABLE | TIMER_CASCADE;
    TIMER_CR(2) = TIMER_ENABLE | TIMER_DIV_1;
#endif
}

/* ---- live notes (KEYS tab): main loop -> render interrupt ------------------
 * A single-producer ring on the ARM9 itself; the interrupt drains it at the
 * start of each block.  While a key is held the 303 is the player's: the
 * sequencer's bass events are ignored until the key is released. */
#define LIVE_Q 16
static volatile ev_t live_q[LIVE_Q];
static volatile uint32_t live_head, live_tail;
static uint8_t live_owns;

void live_note(uint8_t type, uint8_t note, uint8_t accent)
{
    uint32_t h = live_head;
    if (h - live_tail >= LIVE_Q) return;
    live_q[h & (LIVE_Q - 1)].type = type;
    live_q[h & (LIVE_Q - 1)].a = note;
    live_q[h & (LIVE_Q - 1)].b = accent;
    __asm__ volatile("" ::: "memory");
    live_head = h + 1;
}

static AUDIO_CODE void live_drain(void)
{
    while (live_tail != live_head) {
        volatile ev_t *e = &live_q[live_tail & (LIVE_Q - 1)];
        switch (e->type) {
        case EV_NOTE: acid_note(e->a, e->b); live_owns = 1; break;
        case EV_GLIDE: acid_glide(e->a, e->b); live_owns = 1; break;
        case EV_GATE_OFF: acid_gate_off(); live_owns = 0; break;
        case EV_DRUMS: foundry_trigger(e->a, e->b); break;    /* audition */
        default: break;
        }
        live_tail++;
    }
}

static AUDIO_CODE void acid_event(const ev_t *e)
{
    if (live_owns && (e->type == EV_NOTE || e->type == EV_GLIDE || e->type == EV_GATE_OFF)) return;
    switch (e->type) {
    case EV_NOTE: acid_note(e->a, e->b); break;
    case EV_GLIDE: acid_glide(e->a, e->b); break;
    case EV_GATE_OFF: acid_gate_off(); break;
    case EV_STEP: acid_step(); break;
    case EV_KILL: acid_kill(); break;
    default: break;
    }
}

static AUDIO_CODE void drum_event(const ev_t *e)
{
    if (e->type == EV_DRUMS) foundry_trigger(e->a, e->b);
    else if (e->type == EV_KILL) foundry_kill();
}

/* percent x 100 of the block period, for a sum over STATS_WINDOW blocks:
 * sum * 10000 / (256 * 131072) = sum * 10000 >> 25 (no division) */
static inline uint16_t pct(uint32_t sum)
{
    return (uint16_t)(((uint64_t)sum * 10000u) >> 25);
}

AUDIO_CODE void audio_render_block(const evslot_t *evs, uint32_t blk, int16_t *out_l, int16_t *out_r)
{
    uint32_t t[ST_COUNT + 1], i, j, c, total;

    t[0] = clk();
    /* ---- events: snapshot this block's slot (only if it is this block's) */
    nev = 0;
    if (evs->block == blk) {
        nev = evs->n > EV_MAX ? EV_MAX : evs->n;
        for (i = 0; i < nev; i++) evq[i] = evs->ev[i];
    }
    for (i = 0; i < AUDIO_BLOCK; i++) { bus_l[i] = 0; bus_r[i] = 0; acid_buf[i] = 0; }
    t[1] = clk();

    /* ---- ACID ---------------------------------------------------------- */
    live_drain();
    for (c = 0, j = 0; c < AUDIO_BLOCK; c += CTRL_LEN) {
        while (j < nev && evq[j].off <= c) acid_event(&evq[j++]);
        acid_render(acid_buf + c);
    }
    for (i = 0; i < AUDIO_BLOCK; i++) { bus_l[i] = acid_buf[i]; bus_r[i] = acid_buf[i]; }
    t[2] = clk();

    /* ---- FOUNDRY --------------------------------------------------------- */
    for (c = 0, j = 0; c < AUDIO_BLOCK; c += CTRL_LEN) {
        while (j < nev && evq[j].off <= c) drum_event(&evq[j++]);
        foundry_render(bus_l + c, bus_r + c, CTRL_LEN);
    }
    t[3] = clk();

    /* ---- FX chain ------------------------------------------------------- */
    fx_drive(bus_l, bus_r, AUDIO_BLOCK);
    t[4] = clk();
    fx_crush(bus_l, bus_r, AUDIO_BLOCK);
    t[5] = clk();
    fx_delay(bus_l, bus_r, AUDIO_BLOCK);
    t[6] = clk();
    fx_comb(bus_l, bus_r, AUDIO_BLOCK);
    t[7] = clk();
    fx_limit(bus_l, bus_r, out_l, out_r, AUDIO_BLOCK);
    t[8] = clk();
#ifdef __NDS__
    DC_FlushRange(out_l, AUDIO_BLOCK * 2);
    DC_FlushRange(out_r, AUDIO_BLOCK * 2);
#endif
    t[9] = clk();

    /* ---- stats ------------------------------------------------------------- */
    total = t[9] - t[0];
    for (i = 0; i < ST_COUNT; i++) astats.sum[i] += t[i + 1] - t[i];
    if (total > astats.max_total) astats.max_total = total;
    if (++astats.blocks >= STATS_WINDOW) {
        uint32_t tot = 0;
        for (i = 0; i < ST_COUNT; i++) {
            astats.avg_pct[i] = pct(astats.sum[i]);
            tot += astats.sum[i];
            astats.sum[i] = 0;
        }
        astats.avg_total = pct(tot);
        astats.max_total_pct = (uint16_t)(((uint64_t)astats.max_total * 10000u) >> 17);
        astats.max_total = 0;
        astats.blocks = 0;
        astats.windows++;
    }
}
