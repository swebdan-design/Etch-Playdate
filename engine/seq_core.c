/*
 * ETCH DS -- by Synth Centric
 * Sequencer core: ETCH's step logic (src/sequencer/seq.c, ETCH 2.5) with
 * ETCH II's swing-change fix and edit-epoch guard, driven in render time.
 *
 * Runs on the ARM7 inside the block interrupt (and on the host in the unit
 * tests).  For every render block it advances the clock in SEQ_SUB-sample
 * sub-blocks and writes sample-stamped events into the block's event slot;
 * the ARM9 renders the block from them.  Nothing here waits for the ARM9.
 *
 * Clock: ETCH's phase accumulator over a PAIR of 16ths, widened to 32 bits
 * (plus an 8-bit fraction).  The even 16th starts when the phase wraps, the
 * odd one when it enters [swing point, end); a step fires only when the next
 * step has the matching parity (ETCH II's fix: raising SWING while the phase
 * sat between the old and new swing points used to fire the odd step twice).
 * Long-run tempo is exact to 3.5e-9; onsets are quantised to SEQ_SUB samples
 * (0.98 ms at 32 728 Hz; ETCH: 3.9 ms).  No division anywhere.
 *
 * Step semantics (TB-style, unchanged from ETCH):
 *   NOTE   new note; envelopes retrigger unless the previous step slid into it
 *   TIE    previous note sustains, no retrigger
 *   REST   silence; the previous note is released at its gate point
 *   SLIDE  gate held past the gate point, pitch glides into step n+1 if it plays
 *   ACCENT louder, stronger and faster filter sweep (combines with slide)
 *   PROB   (p+1)/8, rolled live each loop with the LIVE dice (never CORROSION's)
 *   COND   1:2 2:2 1:4 2:4 3:4 4:4 FILL !FILL
 */
#include <string.h>
#include "ipc.h"
#include "seq_core.h"

static shared_t *S;

static uint32_t pphase, pinc, swing_pt, gate_even, gate_odd;
static uint16_t pfrac, pinc_frac;
static uint8_t gate_done, next_plays, cur_plays, legato, pending_start, last_region;
static uint8_t held;               /* the ACID gate as the voice will see it */
static uint8_t fill_bars, chain_jump = 0xFF;
static uint8_t tempo_bpm, tempo_swing, tempo_gate;
static uint16_t live_rng = 0x5EED;
static evslot_t *slot;             /* slot being filled                       */
static uint8_t sub_off;            /* sample offset of the current sub-block  */
static uint8_t stress_note;
static uint8_t kill_pending;       /* STOP: send EV_KILL with the next block   */

static const uint8_t gate_frac[4] = { 6, 9, 12, 15 };   /* sixteenths of a step */

/* inc per sub-block = bpm * 2^47 / (30 * BUS_HZ) = bpm * 139978.878 */
#define INC_INT   139978u
#define INC_FRAC8 225u

static uint8_t live_rand(void)
{
    live_rng ^= (uint16_t)(live_rng << 7);
    live_rng ^= (uint16_t)(live_rng >> 9);
    live_rng ^= (uint16_t)(live_rng << 8);
    return (uint8_t)live_rng;
}

static void emit(uint8_t type, uint8_t a, uint8_t b)
{
    uint32_t n = slot->n;
    if (n >= EV_MAX) return;                 /* cannot happen: <= 4 per sub */
    slot->ev[n].off = sub_off;
    slot->ev[n].type = type;
    slot->ev[n].a = a;
    slot->ev[n].b = b;
    slot->n = n + 1;
}

/* the voice interface of ETCH's engine, as events */
static void note_on(uint8_t note, uint8_t acc) { emit(EV_NOTE, note, acc); held = 1; }
static void glide(uint8_t note, uint8_t acc)
{
    if (!held) { note_on(note, acc); return; }   /* ETCH: glide needs a held gate */
    emit(EV_GLIDE, note, acc);
}
static void gate_off(void) { if (held) emit(EV_GATE_OFF, 0, 0); held = 0; }

/* ---- chain / queue (ETCH) ----------------------------------------------- */
static uint8_t chain_next(uint8_t pos)
{
    uint8_t i;
    for (i = 0; i < CHAIN_LEN; i++) {
        pos = (uint8_t)((pos + 1) & (CHAIN_LEN - 1));
        if (S->proj.chain.pat[pos] < NUM_PATTERNS) return pos;
        if (S->proj.chain.pat[pos] == 0xFF) pos = CHAIN_LEN - 1;
    }
    return 0;
}

static uint8_t peek_next_pattern(uint8_t *loop_out)
{
    uint8_t p = S->cur;
    *loop_out = (uint8_t)(S->loop + 1);
    if (S->proj.chain.song && chain_jump != 0xFF) {
        if (S->proj.chain.pat[chain_jump] < NUM_PATTERNS) p = S->proj.chain.pat[chain_jump];
    } else if (S->proj.chain.song) {
        if ((uint8_t)(S->chain_bar + 1) >= S->proj.chain.rep[S->chain_pos]) {
            uint8_t np = S->proj.chain.pat[chain_next(S->chain_pos)];
            if (np < NUM_PATTERNS) p = np;
        }
    } else if (S->queued != 0xFF) {
        p = S->queued;
    }
    if (p != S->cur) *loop_out = 0;
    return p;
}

uint8_t seq_next_bar_pattern(void)
{
    uint8_t lp;
    return peek_next_pattern(&lp);
}

static uint8_t cond_ok(uint8_t cond, uint8_t loop)
{
    switch (cond) {
    case COND_1OF2: return (loop & 1) == 0;
    case COND_2OF2: return (loop & 1) == 1;
    case COND_1OF4: return (loop & 3) == 0;
    case COND_2OF4: return (loop & 3) == 1;
    case COND_3OF4: return (loop & 3) == 2;
    case COND_4OF4: return (loop & 3) == 3;
    case COND_FILL: return S->fill;
    case COND_NOFILL: return !S->fill;
    default: return 1;
    }
}

static uint8_t decide(const pattern_t *p, uint8_t s, uint8_t loop)
{
    uint8_t f = p->flags[s], pr = p->prob[s];
    if ((f & STEP_TYPE_MASK) != STEP_NOTE) return 0;
    if (S->proj.mutes & 1) return 0;
    if (!cond_ok((uint8_t)(pr >> 4), loop)) return 0;
    if ((pr & 7) != 7 && (live_rand() & 7) > (pr & 7)) return 0;
    return 1;
}

static uint8_t next_type(void)
{
    uint8_t lp;
    const pattern_t *np;
    if (S->step == 15) np = &S->proj.pat[peek_next_pattern(&lp)];
    else np = &S->proj.pat[S->cur];
    return np->flags[(S->step + 1) & 15] & STEP_TYPE_MASK;
}

static void bar_boundary(void)
{
    uint8_t prev = S->cur;
    S->bar_total++;
    if (fill_bars && !S->fill_hold) {
        if (--fill_bars == 0) S->fill = 0;
    }
    if (S->proj.chain.song && chain_jump != 0xFF) {
        S->chain_pos = chain_jump;
        S->chain_bar = 0;
        chain_jump = 0xFF;
        if (S->proj.chain.pat[S->chain_pos] < NUM_PATTERNS) S->cur = S->proj.chain.pat[S->chain_pos];
        S->queued = 0xFF;
    } else if (S->proj.chain.song) {
        if (++S->chain_bar >= S->proj.chain.rep[S->chain_pos]) {
            uint8_t np;
            S->chain_bar = 0;
            S->chain_pos = chain_next(S->chain_pos);
            np = S->proj.chain.pat[S->chain_pos];
            if (np < NUM_PATTERNS) S->cur = np;
        }
        S->queued = 0xFF;
    } else if (S->queued != 0xFF) {
        S->cur = S->queued;
        S->queued = 0xFF;
    }
    if (S->cur != prev) S->loop = 0; else S->loop++;
    S->phrase++;
}

/* CORROSION commit at the gate point of step 16, so the new material starts
 * exactly on the next downbeat.  A candidate computed before an edit of its
 * pattern (edit epoch moved on) is refused, as in ETCH II. */
static void evo_commit_point(void)
{
    uint8_t due, lp, np, nphrase = (uint8_t)(S->phrase + 1);
    due = S->evo_request;
    if (S->proj.gen.mode == EVO_DRIFT && (nphrase & 3) == 0) due = 1;
    if (S->proj.gen.mode == EVO_CORRODE && (nphrase & 1) == 0) due = 1;
    if (!due || S->evo_state != EVO_READY) return;
    np = peek_next_pattern(&lp);
    if (S->evo_target != np || (S->proj.pat[np].lock & PLOCK_ALL)) return;
    if (S->evo_epoch != S->pat_epoch[np]) { S->evo_state = EVO_IDLE; S->evo_stale_drops++; return; }
    memcpy(&S->evo_undo, &S->proj.pat[np], sizeof(pattern_t));
    memcpy(&S->proj.pat[np], &S->evo_cand, sizeof(pattern_t));
    S->evo_undo_pat = np;
    S->evo_state = EVO_IDLE;
    S->evo_request = 0;
    S->evo_commits++;
    next_plays = decide(&S->proj.pat[np], 0, lp);      /* re-roll the new downbeat */
}

static void step_advance(void)
{
    const pattern_t *p;
    uint8_t s, f, lanes = 0, lp, l;

    if (pending_start) {
        pending_start = 0;
        S->step = 0;
        cur_plays = 0;
        legato = 0;
        next_plays = decide(&S->proj.pat[S->cur], 0, S->loop);
    } else {
        if (!gate_done) S->gates_late++;               /* a gate was skipped */
        S->step = (uint8_t)((S->step + 1) & 15);
        if (S->step == 0) bar_boundary();
    }
    s = S->step;
    p = &S->proj.pat[S->cur];
    gate_done = 0;
    S->steps_total++;
    emit(EV_STEP, s, S->cur);                          /* accent charge decay, UI */

    /* ---- drums ----------------------------------------------------- */
    {
        const uint16_t *m = S->fill ? S->fill_mask : p->drum;
        for (l = 0; l < NUM_LANES; l++) if (m[l] & (1u << s)) lanes |= (uint8_t)(1u << l);
        lanes &= (uint8_t)~(S->proj.mutes >> 1);
        if (S->stress) lanes = (1u << DS_LANES) - 1;
    }
    if (lanes) {
        uint8_t kits = (uint8_t)((p->kit[0] & 3) | ((p->kit[1] & 3) << 2) |
                                 ((p->kit[2] & 3) << 4) | ((p->kit[3] & 3) << 6));
        emit(EV_DRUMS, lanes, kits);
    }

    /* ---- bass ------------------------------------------------------ */
    if (S->stress) {                                   /* CPU test: worst case */
        stress_note = (uint8_t)((stress_note + 7) & 31);
        if (s & 1) glide((uint8_t)(12 + stress_note), 1);
        else note_on((uint8_t)(12 + stress_note), 1);
        cur_plays = 1;
        legato = 0;
        next_plays = 1;
        return;
    }
    f = p->flags[s];
    if (next_plays) {
        if (legato) glide(p->note[s], (uint8_t)(f & STEP_ACCENT));
        else note_on(p->note[s], (uint8_t)(f & STEP_ACCENT));
        cur_plays = 1;
    } else if ((f & STEP_TYPE_MASK) == STEP_TIE && cur_plays && held) {
        /* sustain: nothing to do */
    } else {
        cur_plays = 0;
        gate_off();
    }
    legato = 0;

    if (s == 15) {
        uint8_t np = peek_next_pattern(&lp);
        next_plays = decide(&S->proj.pat[np], 0, lp);
    } else {
        next_plays = decide(p, (uint8_t)(s + 1), S->loop);
    }
}

static void gate_point(void)
{
    const pattern_t *p = &S->proj.pat[S->cur];
    uint8_t f;
    gate_done = 1;
    S->gates_total++;
    if (S->step == 15) evo_commit_point();
    if (S->stress) return;
    f = p->flags[S->step];
    if (!cur_plays) return;
    if ((f & STEP_SLIDE) && next_plays) { legato = 1; return; }   /* hold + glide */
    if (next_type() == STEP_TIE) return;                          /* sustain      */
    gate_off();
}

/* Tempo, swing and gate length: ETCH's formulas, 32-bit phases. */
static void set_tempo(void)
{
    uint32_t bpm = S->proj.synth.bpm, sw = S->proj.synth.swing, g;
    uint32_t sp, ge, go;
    if (bpm < 60) bpm = 60;
    if (bpm > 220) bpm = 220;
    if (sw > 8) sw = 8;
    g = gate_frac[S->proj.synth.gate & 3];
    sp = (0x8000u + sw * 1311u) << 16;                  /* +4 % of a pair per unit */
    ge = (sp >> 4) * g;
    go = sp + ((0u - sp) >> 4) * g;                      /* (2^32 - sp) * g / 16    */
    pinc = bpm * INC_INT;
    pinc_frac = (uint16_t)(bpm * INC_FRAC8);
    swing_pt = sp;
    gate_even = ge;
    gate_odd = go;
    tempo_bpm = S->proj.synth.bpm;
    tempo_swing = S->proj.synth.swing;
    tempo_gate = S->proj.synth.gate;
}

static void sub_block(void)
{
    uint32_t old;
    uint8_t region, next_odd, fire = 0;
    if (!S->playing) return;
    if (pending_start) { step_advance(); return; }
    old = pphase;
    pfrac = (uint16_t)(pfrac + pinc_frac);
    pphase += pinc + (pfrac >> 8);
    pfrac &= 0xFF;
    region = pphase >= swing_pt;
    next_odd = (uint8_t)((S->step + 1) & 1);
    if (pphase < old) fire = !next_odd;                   /* even 16th: pair wrap  */
    else if (!last_region && region) fire = next_odd;     /* odd 16th: swing point */
    last_region = region;
    if (fire) { step_advance(); return; }
    if (!gate_done && pphase >= ((S->step & 1) ? gate_odd : gate_even)) gate_point();
}

void seq_render_block(uint32_t blk)
{
    uint32_t i;
    slot = &S->ev[blk & (AUDIO_NBLK - 1)];
    slot->n = 0;
    if (kill_pending) {
        kill_pending = 0;
        sub_off = 0;
        emit(EV_KILL, 0, 0);
    }
    if (S->proj.synth.bpm != tempo_bpm || S->proj.synth.swing != tempo_swing ||
        S->proj.synth.gate != tempo_gate)
        set_tempo();
    for (i = 0; i < SEQ_SUBS; i++) {
        sub_off = (uint8_t)(i * SEQ_SUB);
        sub_block();
    }
    slot->block = blk;
}

/* ---- commands from the ARM9 (run with the block interrupt masked) -------- */
static void edited(uint8_t pat)
{
    if (pat < NUM_PATTERNS) S->pat_epoch[pat]++;
    S->evo_state = EVO_IDLE;           /* recompute from the edited pattern */
}

static void start(uint16_t seed)
{
    live_rng = seed ? seed : 0x5EED;
    S->loop = 0;
    S->phrase = 0;
    pphase = 0;
    pfrac = 0;
    last_region = 0;
    S->chain_bar = 0;
    if (S->proj.chain.song) {
        if (S->proj.chain.pat[S->chain_pos] >= NUM_PATTERNS) S->chain_pos = 0;
        if (S->proj.chain.pat[S->chain_pos] < NUM_PATTERNS) S->cur = S->proj.chain.pat[S->chain_pos];
    }
    set_tempo();
    pending_start = 1;
    S->playing = 1;
}

static void stop(void)
{
    S->playing = 0;
    S->fill = 0;
    S->fill_hold = 0;
    fill_bars = 0;
    held = 0;
}

void seq_command(uint32_t v)
{
    uint32_t c = v >> 24, arg = v & 0xFFFFFFu;
    switch (c) {
    case CMD_START:
        if (!S->playing) start((uint16_t)arg);
        break;
    case CMD_STOP:
        stop();
        kill_pending = 1;
        break;
    case CMD_TEMPO:
        set_tempo();
        break;
    case CMD_EDITED:
        if (arg < NUM_PATTERNS) S->pat_epoch[arg]++;
        else {                                         /* a new project */
            uint8_t i;
            for (i = 0; i < NUM_PATTERNS; i++) S->pat_epoch[i]++;
            S->evo_undo_pat = 0xFF;
            S->queued = 0xFF;
            S->chain_pos = 0;
        }
        S->evo_state = EVO_IDLE;
        break;
    case CMD_EVO_DROP:
        if (S->evo_state == EVO_READY && S->evo_target != seq_next_bar_pattern())
            S->evo_state = EVO_IDLE;
        break;
    case CMD_UNDO:
        if (S->evo_undo_pat < NUM_PATTERNS) {
            memcpy(&S->proj.pat[S->evo_undo_pat], &S->evo_undo, sizeof(pattern_t));
            S->pat_epoch[S->evo_undo_pat]++;
            S->evo_undo_pat = 0xFF;
            S->evo_state = EVO_IDLE;
            S->evo_request = 0;
        }
        break;
    case CMD_STRESS:
        S->stress = (uint8_t)(arg != 0);
        break;
    case CMD_SET_BASS: {
        uint32_t pat = (arg >> 15) & 31, st = (arg >> 11) & 15;
        pattern_t *p = &S->proj.pat[pat];
        p->note[st] = (uint8_t)((arg >> 5) & 63);
        p->flags[st] = (uint8_t)(arg & 31);
        edited((uint8_t)pat);
        break; }
    case CMD_SET_DRUM: {
        uint32_t pat = (arg >> 7) & 31, lane = (arg >> 5) & 3, st = (arg >> 1) & 15;
        pattern_t *p = &S->proj.pat[pat];
        if (arg & 1) p->drum[lane] |= (uint16_t)(1u << st);
        else p->drum[lane] &= (uint16_t)~(1u << st);
        edited((uint8_t)pat);
        break; }
    case CMD_REPLACE: {
        uint8_t pat = (uint8_t)(arg & 31);
        if (arg & 0x100) {
            memcpy(&S->evo_undo, &S->proj.pat[pat], sizeof(pattern_t));
            S->evo_undo_pat = pat;
        }
        memcpy(&S->proj.pat[pat], &S->edit_pat, sizeof(pattern_t));
        edited(pat);
        S->edit_ack++;
        break; }
    case CMD_FILL:
        if (arg == FILL_LATCH_OFF) {
            S->fill_hold = 0; S->fill = 0; fill_bars = 0;
            break;
        }
        memcpy(S->fill_mask, S->fill_req, sizeof S->fill_mask);
        if (arg == FILL_LATCH_ON) { S->fill_hold = 1; S->fill = 1; fill_bars = 1; }
        else if (arg == FILL_ONESHOT) {
            /* a fill asked for late in the bar runs through the next bar too */
            fill_bars = (S->playing && S->step >= 12) ? 2 : 1;
            S->fill = 1;
        }
        break;
    case CMD_EVOLVE:
        if (!(S->proj.pat[seq_next_bar_pattern()].lock & PLOCK_ALL)) S->evo_request = 1;
        break;
    case CMD_MUTES:
        S->proj.mutes = (uint8_t)arg;
        break;
    case CMD_QUEUE:                                    /* ETCH's seq_queue_pattern */
        if (arg >= NUM_PATTERNS) break;
        if (!S->playing) { S->cur = (uint8_t)arg; S->queued = 0xFF; }
        else S->queued = (uint8_t)arg;
        S->evo_state = EVO_IDLE;
        break;
    case CMD_CHAIN: {
        uint8_t pos = (uint8_t)((arg >> 16) & (CHAIN_LEN - 1)), pat = (uint8_t)(arg >> 8), rep = (uint8_t)arg;
        S->proj.chain.pat[pos] = pat < NUM_PATTERNS ? pat : 0xFF;
        S->proj.chain.rep[pos] = rep < 1 ? 1 : rep > 8 ? 8 : rep;
        break; }
    case CMD_SONG:
        S->proj.chain.song = (uint8_t)(arg != 0);
        S->queued = 0xFF;
        S->evo_state = EVO_IDLE;
        break;
    case CMD_SET_KIT: {
        uint32_t pat = (arg >> 4) & 31, lane = (arg >> 2) & 3;
        S->proj.pat[pat].kit[lane] = (uint8_t)(arg & 3);
        edited((uint8_t)pat);
        break; }
    case CMD_JUMP:                                     /* ETCH's seq_jump_chain */
        arg &= CHAIN_LEN - 1;
        if (S->proj.chain.pat[arg] >= NUM_PATTERNS) break;
        if (!S->playing) {
            S->chain_pos = (uint8_t)arg; S->chain_bar = 0; S->cur = S->proj.chain.pat[arg];
        } else chain_jump = (uint8_t)arg;
        S->evo_state = EVO_IDLE;
        break;
    default:
        break;
    }
}

void seq_init(shared_t *sh)
{
    S = sh;
    S->playing = 0;
    S->queued = 0xFF;
    S->evo_undo_pat = 0xFF;
    S->evo_state = EVO_IDLE;
    tempo_bpm = 0;
    set_tempo();
}
