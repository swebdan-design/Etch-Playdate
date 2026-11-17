/*
 * ETCH DS -- by Synth Centric
 * Host tests for the ARM7 sequencer core (common/seq_core.c): ETCH's step
 * rules, the fixed swing clock, gate points, the CORROSION commit point and
 * the edit-epoch guard.  The core is driven block by block exactly as the
 * ARM7 block interrupt drives it; events are collected from the slots.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "seq_core.h"
#include "corrosion.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static shared_t sh;

typedef struct { uint64_t t; ev_t e; } tev_t;
static tev_t evlog[200000];
static int nlog;
static uint32_t blk;

static void run_blocks(uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++, blk++) {
        evslot_t *s = &sh.ev[blk & (AUDIO_NBLK - 1)];
        seq_render_block(blk);
        for (k = 0; k < s->n && nlog < (int)(sizeof evlog / sizeof evlog[0]); k++) {
            evlog[nlog].t = (uint64_t)blk * AUDIO_BLOCK + s->ev[k].off;
            evlog[nlog].e = s->ev[k];
            nlog++;
        }
    }
}

static void setup(uint8_t bpm, uint8_t swing)
{
    memset(&sh, 0, sizeof sh);
    pattern_clear(&sh.proj.pat[0]);
    sh.proj.synth.bpm = bpm;
    sh.proj.synth.swing = swing;
    sh.proj.synth.gate = 1;
    sh.proj.chain.song = 0;
    sh.proj.gen.mode = EVO_STABLE;
    seq_init(&sh);
    nlog = 0;
    blk = 0;
}

static int count(uint8_t type) { int i, n = 0; for (i = 0; i < nlog; i++) n += evlog[i].e.type == type; return n; }

/* 1. tempo: steps per minute = 4 * bpm, and the long-run step rate is exact */
static void test_tempo(void)
{
    static const uint8_t bpms[] = { 60, 97, 124, 140, 174, 220 };
    unsigned i;
    for (i = 0; i < sizeof bpms; i++) {
        uint64_t t_first = 0, t_last = 0;
        int k, n = 0;
        double per, expect;
        setup(bpms[i], 0);
        seq_command(CMD(CMD_START, 1));
        run_blocks((uint32_t)(FS_HZ * 60u / AUDIO_BLOCK));         /* one minute */
        for (k = 0; k < nlog; k++) if (evlog[k].e.type == EV_STEP) { if (!n) t_first = evlog[k].t; t_last = evlog[k].t; n++; }
        per = (double)(t_last - t_first) / (n - 1);
        expect = (16756991.0 / 512.0) * 15.0 / bpms[i];
        CHECK(abs(n - 4 * bpms[i]) <= 1, "bpm %d: %d steps in a minute", bpms[i], n);
        CHECK(per > expect * (1 - 1e-4) && per < expect * (1 + 1e-4),
              "bpm %d: step period %.3f samples, expected %.3f", bpms[i], per, expect);
    }
}

/* 2. swing moves only the odd 16th, by +4 % of a pair per unit; even steps stay on the grid */
static void test_swing(void)
{
    uint8_t sw;
    for (sw = 0; sw <= 8; sw++) {
        uint64_t t[8];
        int k, n = 0;
        double pair = (16756991.0 / 512.0) * 30.0 / 120;
        setup(120, sw);
        seq_command(CMD(CMD_START, 1));
        run_blocks(400);
        for (k = 0; k < nlog && n < 8; k++) if (evlog[k].e.type == EV_STEP) t[n++] = evlog[k].t;
        {
            double even = (double)(t[2] - t[0]), odd = (double)(t[3] - t[2]);
            double want = pair * (0.5 + sw * 1311.0 / 65536.0);
            CHECK(even > pair - SEQ_SUB - 1 && even < pair + SEQ_SUB + 1, "swing %d: pair %.0f vs %.0f", sw, even, pair);
            CHECK(odd > want - SEQ_SUB - 1 && odd < want + SEQ_SUB + 1, "swing %d: odd offset %.0f vs %.0f", sw, odd, want);
        }
    }
}

/* 3. ETCH II's swing-change fix: raising SWING while the phase sits between the
 *    old and new swing points must not fire the odd step twice, and every
 *    gate point must still be reached (220 BPM, swing 0 -> 8 at every phase). */
static void test_swing_change(void)
{
    uint32_t start;
    for (start = 0; start < 64; start++) {
        uint32_t steps_before;
        setup(220, 0);
        sh.proj.synth.gate = 3;
        seq_command(CMD(CMD_START, 1));
        run_blocks(40 + start);
        steps_before = sh.steps_total;
        sh.proj.synth.swing = 8;                   /* picked up at the next block */
        run_blocks(1);
        sh.proj.synth.swing = 0;
        run_blocks(1);
        sh.proj.synth.swing = 8;
        run_blocks(2000);
        CHECK(sh.gates_late == 0, "start %u: %u gate points skipped", start, sh.gates_late);
        (void)steps_before;
        {   /* parity: steps alternate even/odd with no doubles */
            int k, last = -1, bad = 0;
            for (k = 0; k < nlog; k++) if (evlog[k].e.type == EV_STEP) {
                if (last >= 0 && evlog[k].e.a != ((last + 1) & 15)) bad++;
                last = evlog[k].e.a;
            }
            CHECK(bad == 0, "start %u: %d out-of-order steps", start, bad);
        }
    }
}

/* 4. step rules: note, rest, tie, slide, accent */
static void test_rules(void)
{
    pattern_t *p;
    int k, notes = 0, glides = 0, offs = 0;
    setup(120, 0);
    p = &sh.proj.pat[0];
    /* step 0 note+accent, 1 tie, 2 rest, 3 note slide -> 4 note, 5 rest */
    p->flags[0] = STEP_NOTE | STEP_ACCENT; p->note[0] = 9;
    p->flags[1] = STEP_TIE;
    p->flags[2] = STEP_REST;
    p->flags[3] = STEP_NOTE | STEP_SLIDE; p->note[3] = 12;
    p->flags[4] = STEP_NOTE; p->note[4] = 21;
    seq_command(CMD(CMD_START, 1));
    run_blocks((uint32_t)(FS_HZ * 2 / AUDIO_BLOCK));            /* one bar at 120 */
    for (k = 0; k < nlog; k++) {
        ev_t *e = &evlog[k].e;
        if (e->type == EV_NOTE) notes++;
        if (e->type == EV_GLIDE) { glides++; CHECK(e->a == 21, "glide target %d", e->a); }
        if (e->type == EV_GATE_OFF) offs++;
    }
    CHECK(notes == 2, "two retriggered notes per bar (got %d)", notes);
    CHECK(glides == 1, "one slide per bar (got %d)", glides);
    /* the tie holds step 0 into step 1: first gate-off is after step 1's onset */
    {
        uint64_t t_step1 = 0, t_off = 0;
        for (k = 0; k < nlog; k++) {
            if (evlog[k].e.type == EV_STEP && evlog[k].e.a == 1 && !t_step1) t_step1 = evlog[k].t;
            if (evlog[k].e.type == EV_GATE_OFF && !t_off) t_off = evlog[k].t;
        }
        CHECK(t_off > t_step1, "tie sustains: gate-off at %llu, step 2 at %llu",
              (unsigned long long)t_off, (unsigned long long)t_step1);
    }
    CHECK(offs >= 2, "rests release (got %d gate-offs)", offs);
    for (k = 0; k < nlog; k++) if (evlog[k].e.type == EV_NOTE && evlog[k].e.a == 9) {
        CHECK(evlog[k].e.b != 0, "accent flag passed through");
        break;
    }
}

/* 5. probability (live dice) and conditions */
static void test_prob_cond(void)
{
    pattern_t *p;
    int k, n, bars = 64;
    setup(200, 0);
    p = &sh.proj.pat[0];
    p->flags[0] = STEP_NOTE; p->prob[0] = 3;                        /* 4/8 */
    p->flags[8] = STEP_NOTE; p->prob[8] = (COND_1OF4 << 4) | 7;     /* 1:4 */
    p->note[8] = 30;
    seq_command(CMD(CMD_START, 0x1234));
    run_blocks((uint32_t)((uint64_t)FS_HZ * 60 * 4 * bars / 200 / AUDIO_BLOCK));
    n = 0;
    for (k = 0; k < nlog; k++) if (evlog[k].e.type == EV_NOTE && evlog[k].e.a != 30) n++;
    CHECK(n > bars / 2 - 14 && n < bars / 2 + 14, "prob 4/8 played %d of %d", n, bars);
    n = 0;
    for (k = 0; k < nlog; k++) if (evlog[k].e.type == EV_NOTE && evlog[k].e.a == 30) n++;
    CHECK(n >= bars / 4 - 1 && n <= bars / 4 + 1, "cond 1:4 played %d of %d", n, bars);
}

/* 6. CORROSION commit lands on the downbeat; the edit epoch refuses stale candidates */
static void test_commit(void)
{
    gen_t g;
    pattern_t cand;
    int k;
    uint64_t t_commit_step = 0;
    setup(124, 1);
    memset(&g, 0, sizeof g);
    corr_code_decode(&g, "0F1C90095870");
    g.mode = EVO_DRIFT; g.mutation = 5;
    sh.proj.gen = g;
    corr_generate(&sh.proj.pat[0], &g, GEN_ALL);
    corr_mutate(&cand, &sh.proj.pat[0], &sh.proj.pat[0], &g, EVO_DRIFT, 5, 77);
    memcpy(&sh.evo_cand, &cand, sizeof cand);
    sh.evo_target = 0;
    sh.evo_epoch = sh.pat_epoch[0];
    sh.evo_state = EVO_READY;
    seq_command(CMD(CMD_START, 1));
    run_blocks((uint32_t)(FS_HZ * 10 / AUDIO_BLOCK));
    CHECK(sh.evo_commits == 1, "DRIFT committed once (%u)", sh.evo_commits);
    CHECK(!memcmp(&sh.proj.pat[0], &cand, sizeof cand), "candidate is the pattern");
    CHECK(sh.evo_undo_pat == 0, "undo kept");
    (void)t_commit_step;
    for (k = 0; k < nlog; k++) (void)0;

    /* stale: candidate computed, then the pattern is edited -> refused */
    setup(124, 0);
    sh.proj.gen = g;
    corr_generate(&sh.proj.pat[0], &g, GEN_ALL);
    memcpy(&sh.evo_cand, &cand, sizeof cand);
    sh.evo_target = 0;
    sh.evo_epoch = sh.pat_epoch[0];
    sh.evo_state = EVO_READY;
    seq_command(CMD(CMD_EDITED, 0));               /* epoch moves on, state IDLE */
    sh.evo_state = EVO_READY;                      /* a racing READY with the old epoch */
    seq_command(CMD(CMD_START, 1));
    run_blocks((uint32_t)(FS_HZ * 10 / AUDIO_BLOCK));
    CHECK(sh.evo_commits == 0, "stale candidate refused");
    CHECK(sh.evo_stale_drops == 1, "stale drop counted (%u)", sh.evo_stale_drops);

    /* undo restores the previous generation and bumps the epoch */
    setup(124, 0);
    sh.proj.gen = g;
    corr_generate(&sh.proj.pat[0], &g, GEN_ALL);
    {
        pattern_t orig = sh.proj.pat[0];
        memcpy(&sh.evo_cand, &cand, sizeof cand);
        sh.evo_target = 0; sh.evo_epoch = 0; sh.evo_state = EVO_READY;
        seq_command(CMD(CMD_START, 1));
        run_blocks((uint32_t)(FS_HZ * 10 / AUDIO_BLOCK));
        seq_command(CMD(CMD_UNDO, 0));
        CHECK(!memcmp(&sh.proj.pat[0], &orig, sizeof orig), "undo restores");
        CHECK(sh.pat_epoch[0] == 1, "undo is an edit");
    }
}

/* 7. the commit happens at step 16's gate point, i.e. between the last
 *    step of a bar and the next downbeat */
static void test_commit_timing(void)
{
    gen_t g;
    pattern_t cand;
    int k;
    uint8_t seen_gen = 0;
    setup(124, 0);
    memset(&g, 0, sizeof g);
    corr_code_decode(&g, "0F1C90095870");
    g.mode = EVO_CORRODE;
    sh.proj.gen = g;
    corr_generate(&sh.proj.pat[0], &g, GEN_ALL);
    cand = sh.proj.pat[0];
    cand.gen = 9;
    sh.evo_cand = cand;
    sh.evo_target = 0; sh.evo_epoch = 0; sh.evo_state = EVO_READY;
    seq_command(CMD(CMD_START, 1));
    /* run step by step; the pattern must change while step == 15 */
    for (k = 0; k < 4000 && !seen_gen; k++) {
        uint8_t before = sh.proj.pat[0].gen, step = sh.step;
        run_blocks(1);
        if (sh.proj.pat[0].gen != before) {
            seen_gen = 1;
            CHECK(step == 15 && sh.step == 15, "commit inside step 16 (step %d -> %d)", step, sh.step);
            CHECK(((sh.phrase + 1) & 1) == 0, "CORRODE commits every 2 bars");
        }
    }
    CHECK(seen_gen, "committed");
}

/* 8. stop sends a kill; stress mode fires every lane every step */
static void test_stop_stress(void)
{
    int k, drums = 0, kills;
    setup(140, 0);
    seq_command(CMD(CMD_START, 1));
    seq_command(CMD(CMD_STRESS, 1));
    run_blocks(1000);
    for (k = 0; k < nlog; k++) if (evlog[k].e.type == EV_DRUMS) { drums++; CHECK(evlog[k].e.a == 0x3F, "six lanes"); }
    CHECK(drums == count(EV_STEP), "a drum event every step");
    seq_command(CMD(CMD_STOP, 0));
    run_blocks(4);
    kills = count(EV_KILL);
    CHECK(kills == 1, "stop sends one kill (%d)", kills);
    CHECK(!sh.playing, "stopped");
}

/* 9. GUI edits go through the ARM7: step fields, drum hits, whole-pattern
 *    replace with undo, fills, evolve request, mutes; each edit moves the
 *    edit epoch so a stale CORROSION candidate cannot overwrite it */
static void test_edit_commands(void)
{
    pattern_t *p, orig, rep;
    int k, offs;
    setup(130, 0);
    p = &sh.proj.pat[0];
    seq_command(CMD(CMD_SET_BASS, CMD_BASS_ARG(0, 5, 33, STEP_NOTE | STEP_ACCENT)));
    CHECK(p->note[5] == 33 && p->flags[5] == (STEP_NOTE | STEP_ACCENT), "set bass step");
    CHECK(sh.pat_epoch[0] == 1, "bass edit moves the epoch");
    seq_command(CMD(CMD_SET_DRUM, CMD_DRUM_ARG(0, 2, 9, 1)));
    CHECK(p->drum[2] == (1u << 9), "drum on");
    seq_command(CMD(CMD_SET_DRUM, CMD_DRUM_ARG(0, 2, 9, 0)));
    CHECK(p->drum[2] == 0, "drum off");
    CHECK(sh.pat_epoch[0] == 3, "every edit moves the epoch");

    orig = *p;
    memset(&rep, 0, sizeof rep);
    rep.drum[0] = 0x1111; rep.gen = 42;
    sh.edit_pat = rep;
    seq_command(CMD(CMD_REPLACE, 0 | 0x100));
    CHECK(!memcmp(p, &rep, sizeof rep), "replace");
    CHECK(sh.edit_ack == 1, "replace acknowledged");
    seq_command(CMD(CMD_UNDO, 0));
    CHECK(!memcmp(p, &orig, sizeof orig), "replace is undoable");

    /* a stale candidate after an edit is refused */
    sh.evo_cand = rep; sh.evo_target = 0; sh.evo_epoch = (uint8_t)(sh.pat_epoch[0] - 1); sh.evo_state = EVO_READY;
    sh.proj.gen.mode = EVO_CORRODE;
    seq_command(CMD(CMD_START, 1));
    run_blocks((uint32_t)(FS_HZ * 5 / AUDIO_BLOCK));
    CHECK(sh.evo_commits == 0 && sh.evo_stale_drops == 1, "edited pattern not overwritten");

    /* one-shot fill: drums follow the fill masks until the bar line */
    setup(130, 0);
    sh.proj.pat[0].drum[0] = 0x0001;
    sh.fill_req[0] = 0xFFFF;
    seq_command(CMD(CMD_START, 1));
    run_blocks(4);
    seq_command(CMD(CMD_FILL, FILL_ONESHOT));
    CHECK(sh.fill == 1, "fill on");
    run_blocks((uint32_t)(FS_HZ * 4 / AUDIO_BLOCK));
    CHECK(sh.fill == 0, "one-shot fill ends at the bar line");
    seq_command(CMD(CMD_FILL, FILL_LATCH_ON));
    run_blocks((uint32_t)(FS_HZ * 4 / AUDIO_BLOCK));
    CHECK(sh.fill == 1 && sh.fill_hold == 1, "latched fill holds");
    seq_command(CMD(CMD_FILL, FILL_LATCH_OFF));
    CHECK(sh.fill == 0, "latch off");

    seq_command(CMD(CMD_EVOLVE, 0));
    CHECK(sh.evo_request == 1, "evolve requested");
    seq_command(CMD(CMD_MUTES, 0x03));
    nlog = 0;
    sh.proj.pat[0].flags[0] = STEP_NOTE;
    run_blocks((uint32_t)(FS_HZ * 4 / AUDIO_BLOCK));
    offs = 0;
    for (k = 0; k < nlog; k++) {
        if (evlog[k].e.type == EV_NOTE) offs++;
        if (evlog[k].e.type == EV_DRUMS && (evlog[k].e.a & 1)) offs += 100;
    }
    CHECK(offs == 0, "muted bass and kick stay silent (%d)", offs);
}

int main(void)
{
    test_tempo();
    test_swing();
    test_swing_change();
    test_rules();
    test_prob_cond();
    test_commit();
    test_commit_timing();
    test_stop_stress();
    test_edit_commands();
    printf("test_seq: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
