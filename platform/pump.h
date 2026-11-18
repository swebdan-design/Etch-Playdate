/*
 * ETCH CRANK -- by Synth Centric
 * The audio pump: everything the Playdate's audio callback does.
 *
 * ETCH DS split this between two CPUs: the ARM7's block interrupt ran the
 * sequencer, the ARM9's render interrupt ran the engine.  On the Playdate
 * both run inside the one audio callback, which preempts the update loop
 * exactly as the DS's render interrupt preempted the ARM9's main loop.  For
 * every engine block (AUDIO_BLOCK = 128 samples at 32 728.5 Hz, 3.91 ms):
 *
 *   1. drain the command ring (the update loop's app_send) into seq_command,
 *      as the ARM7's FIFO interrupt ran them between two blocks; platform
 *      parameters (the crank's targets) are taken here too
 *   2. seq_render_block: ETCH's step rules, ETCH II's swing fix and edit
 *      epoch, sample-accurate in render time (engine/seq_core.c, unchanged)
 *   3. audio_render_block: ACID -> FOUNDRY -> FX (engine/, unchanged).  The
 *      crank rides in through one link-time wrap of acid_render (crank.c)
 *   4. the block goes into a small ring that the resampler reads
 *
 * The callback asks for N output frames at 44.1 kHz; pump_pull() renders as
 * many engine blocks as the resampler needs (0, 1 or 2) and converts.
 *
 * Portable C, no Playdate calls: the host tests and the ARM parity runs use
 * this exact file.  Nothing here allocates, blocks, locks or waits.
 */
#ifndef ETCH_CRANK_PUMP_H
#define ETCH_CRANK_PUMP_H
#include <stdint.h>
#include "ipc.h"

#define PUMP_CMDQ   256u            /* commands in flight (power of 2)     */
#define PUMP_RING   512u            /* engine frames kept for the resampler */

/* platform commands share the ring with ETCH DS's (CMD(c, arg), c >= 0xC0) */
enum {
    PCMD_CUTOFF = 0xC0,             /* arg = live cutoff, Q8 (0 .. 127 << 8)  */
    PCMD_RES,                       /* arg = live resonance, Q8               */
    PCMD_QUARTER,                   /* arg = control ticks in a quarter step  */
    PCMD_RESET                      /* a project load: the engine's own values */
};

void     pump_init(shared_t *sh);   /* seq_init, clear the rings           */
/* fill len output frames (44.1 kHz) of each channel */
void     pump_pull(int16_t *left, int16_t *right, uint32_t len);
/* render one engine block straight into l/r (no resampler): the parity test */
void     pump_block(int16_t *l, int16_t *r);
uint32_t pump_blocks(void);         /* engine blocks rendered so far        */

/* a tap on every engine block (the parity test); NULL in the game */
extern void (*pump_tap)(const int16_t *l, const int16_t *r, uint32_t blk);
extern volatile uint32_t pump_cmd_drops;
/* the resampler test feeds its own engine-rate blocks; NULL in the game */
extern void (*pump_source)(int16_t *l, int16_t *r);

/* the newest engine-rate audio for the scope (mono), from a rising zero
 * crossing so the trace stands still; returns the number of samples */
uint32_t pump_scope(int16_t *out, uint32_t n);

/* cost of the stages around the DS engine, in the same units as astats
 * (host_clock ticks; percent x 100 of a block period, per 256 blocks) */
typedef struct {
    uint32_t seq_sum, rs_sum, cb_sum, blocks;
    uint32_t cb_max;                       /* worst callback in the window  */
    uint16_t seq_pct, rs_pct;              /* last window                   */
    uint32_t cb_sum_last, cb_max_last;     /* raw ticks, last window        */
    uint32_t frames_last;                  /* output frames in that window  */
    uint32_t frames;
    uint32_t windows;
    uint32_t callbacks;
} pump_stats_t;
extern volatile pump_stats_t pstats;
extern uint32_t (*host_clock)(void);       /* render.arm.c's clock hook     */

#endif
