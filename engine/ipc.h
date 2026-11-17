/*
 * ETCH DS -- by Synth Centric
 * ARM9 <-> ARM7 contract: the shared block, the event ring and the commands.
 *
 * Memory layout (decided with the user, see docs/ARCHITECTURE.md):
 *   - One shared_t lives in main RAM.  The ARM7 has no cache and uses it
 *     directly; the ARM9 touches it ONLY through the uncached mirror
 *     (memUncached), so no cache maintenance is ever needed for it.
 *   - The audio ring (two int16 buffers, L and R) also lives in main RAM.  The
 *     ARM9 renders into it through the cache and flushes each finished block;
 *     the sound hardware reads it directly.
 *
 * Clock (decided with the user): the ARM7 owns the sequencer.  TIMER0 on the
 * ARM7 runs at bus/1024, which is exactly one tick per output sample because
 * the sound channels use a period of 512 at bus/2.  It interrupts once per
 * render block.  In that interrupt the ARM7 runs ETCH's step logic for the
 * block AUDIO_LEAD blocks ahead of the hardware read position, writes the
 * resulting events (sample-stamped) into the event slot for that block, and
 * rings the ARM9 (IPC sync IRQ), which renders the block from those events.
 */
#ifndef ETCH_DS_IPC_H
#define ETCH_DS_IPC_H

#include <stdint.h>
#include "etch.h"

/* ---- audio format --------------------------------------------------------- */
#define SND_TIMER        512u        /* sound channel period, bus/2 clocks   */
#define BUS_HZ           33513982u
#define FS_HZ            32728u      /* 16 756 991 / 512 = 32 728.5 Hz       */
#define AUDIO_BLOCK      128u        /* samples per render block (3.91 ms)  */
#define AUDIO_NBLK       4u          /* blocks in the hardware ring         */
#define AUDIO_LEAD       2u          /* blocks rendered ahead of playback   */
#define SEQ_SUB          32u         /* sequencer granularity (0.98 ms)      */
#define SEQ_SUBS         (AUDIO_BLOCK / SEQ_SUB)
#define AUDIO_RING       (AUDIO_BLOCK * AUDIO_NBLK)

/* ---- DS extensions to ETCH's 4 drum lanes ---------------------------------- */
#define DS_LANES         6           /* KICK SNARE/CLAP CH OH METAL NOISE    */
#define LANE_METAL       4
#define LANE_NOISE       5

/* ---- events (ARM7 -> ARM9), stamped with a sample offset in the block ------ */
enum {
    EV_NONE = 0,
    EV_NOTE,        /* a = note (0 = C1), b = accent                         */
    EV_GLIDE,       /* a = note, b = accent: slide into it, gate stays held  */
    EV_GATE_OFF,    /* release the ACID voice                                */
    EV_DRUMS,       /* a = lane mask (6 bits), b = kits of lanes 0-3, 2 bits each */
    EV_STEP,        /* a = step 0..15, b = pattern: once per 16th            */
    EV_KILL         /* stop: everything silent                               */
};

typedef struct {
    uint8_t off;    /* sample offset in the block, multiple of SEQ_SUB       */
    uint8_t type;
    uint8_t a;
    uint8_t b;
} ev_t;

#define EV_MAX 16
typedef struct {
    uint32_t block;               /* block number these events belong to   */
    uint32_t n;
    ev_t     ev[EV_MAX];
} evslot_t;

/* ---- CORROSION hand-off (ETCH's protocol + ETCH II's edit epoch) ----------- */
#define EVO_IDLE   0              /* ARM9 must (re)compute a candidate        */
#define EVO_READY  1              /* evo_cand holds a mutation for evo_target */

/* ---- commands (ARM9 -> ARM7) on FIFO_USER_02, value = cmd << 24 | arg ------ */
#ifdef __NDS__
#define FIFO_ETCH_INIT   FIFO_USER_01   /* address of shared_t              */
#define FIFO_ETCH_CMD    FIFO_USER_02
#endif
enum {
    CMD_START = 1,    /* arg = live dice seed (16 bits)                       */
    CMD_STOP,
    CMD_TEMPO,        /* bpm / swing / gate changed in proj.synth             */
    CMD_EDITED,       /* arg = pattern (0xFF = all): new edit epoch           */
    CMD_EVO_DROP,     /* the prepared candidate targets the wrong pattern     */
    CMD_UNDO,
    CMD_STRESS,       /* arg = 1: CPU test, every step fires ACID + 6 lanes   */
    /* pattern edits: the ARM7 is the only writer of pattern memory while it
     * plays, so edits compose with CORROSION commits instead of racing them */
    CMD_SET_BASS,     /* arg = pat<<15 | step<<11 | note<<5 | flags(5 bits)   */
    CMD_SET_DRUM,     /* arg = pat<<7 | lane<<5 | step<<1 | on                */
    CMD_REPLACE,      /* arg = pat | undo<<8: copy edit_pat in (GEN, REGEN)   */
    CMD_FILL,         /* arg = FILL_*; masks in fill_req                      */
    CMD_EVOLVE,       /* evolve at the next bar line (ETCH's SELECT+A)        */
    CMD_MUTES,        /* arg = mutes byte (bit0 bass, bit1..4 lanes)          */
    CMD_QUEUE,        /* arg = pattern: plays from the next bar line (ETCH)   */
    CMD_CHAIN,        /* arg = pos<<16 | pat<<8 | rep  (pat 0xFF = END)       */
    CMD_SONG,         /* arg = 1: follow the chain                            */
    CMD_JUMP,         /* arg = chain position: from the next bar line         */
    CMD_SET_KIT       /* arg = pat<<4 | lane<<2 | kit                         */
};
enum { FILL_ONESHOT, FILL_LATCH_ON, FILL_LATCH_OFF, FILL_UPDATE };
#define CMD_BASS_ARG(pat, step, note, flags) \
    (((uint32_t)(pat) << 15) | ((uint32_t)(step) << 11) | ((uint32_t)(note) << 5) | ((uint32_t)(flags) & 31u))
#define CMD_DRUM_ARG(pat, lane, step, on) \
    (((uint32_t)(pat) << 7) | ((uint32_t)(lane) << 5) | ((uint32_t)(step) << 1) | ((on) ? 1u : 0u))
#define CMD(c, arg) (((uint32_t)(c) << 24) | ((uint32_t)(arg) & 0xFFFFFFu))

/* ---- the shared block ------------------------------------------------------ */
typedef struct {
    /* written by the ARM9 once at boot */
    uint32_t magic;
    int16_t *buf_l;               /* AUDIO_RING samples each, main RAM       */
    int16_t *buf_r;

    /* the one project: ETCH's data model, byte-identical layout */
    project_t proj;

    /* sequencer state, owned by the ARM7 (render time, not wall time) */
    volatile uint8_t playing, step, cur, queued;
    volatile uint8_t chain_pos, chain_bar, loop, bar_total;
    volatile uint8_t phrase, fill, fill_hold, stress;
    uint16_t fill_mask[NUM_LANES];

    /* CORROSION hand-off.  ARM9 sets READY (only from IDLE); ARM7 sets IDLE.
     * pat_epoch is written only by the ARM7 (CMD_EDITED, undo). */
    volatile uint8_t evo_state, evo_target, evo_epoch, evo_request;
    volatile uint8_t evo_commits, evo_undo_pat, evo_stale_drops, pad0;
    volatile uint8_t pat_epoch[NUM_PATTERNS];
    pattern_t evo_cand;
    /* whole-pattern edits (GEN, REGEN): the ARM9 writes edit_pat, sends
     * CMD_REPLACE and waits for edit_ack to move before reusing it */
    pattern_t edit_pat;
    volatile uint32_t edit_ack;
    uint16_t fill_req[NUM_LANES];
    pattern_t evo_undo;

    /* audio stream bookkeeping */
    volatile uint32_t blk_played;     /* ARM7: block now leaving the speaker */
    volatile uint32_t blk_requested;  /* ARM7: blocks [0, n) may be rendered */
    volatile uint32_t blk_done;       /* ARM9: blocks [0, n) are rendered    */
    volatile uint32_t dropouts;       /* ARM7: blocks that were not ready    */
    volatile uint32_t steps_total, gates_total, gates_late;
    volatile uint32_t arm7_ticks_last, arm7_ticks_max;   /* bus clocks / 64 */

    evslot_t ev[AUDIO_NBLK];
} shared_t;

#define SHARED_MAGIC 0xE7C4D500u

/* bars until the next scheduled evolution (0 = none scheduled) */
static inline uint8_t bars_to_evolution(const shared_t *sh)
{
    uint8_t ph = sh->phrase;
    if (sh->proj.gen.mode == EVO_DRIFT) return (uint8_t)(4 - (ph & 3));
    if (sh->proj.gen.mode == EVO_CORRODE) return (uint8_t)(2 - (ph & 1));
    return 0;
}

#endif
