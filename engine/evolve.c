/*
 * ETCH DS -- by Synth Centric
 * CORROSION scheduling on the ARM9 main loop: ETCH's evo_service, DRIFT's
 * per-pattern anchors, entropy and living-song sound drift (ETCH src/main.c),
 * with ETCH II's edit-epoch guard.
 *
 * CORROSION itself (corrosion/) is used unmodified.  The main loop prepares a
 * mutation of the pattern that will play after the next bar line, on a cached
 * working copy; the ARM7 commits it at the gate point of step 16 so it lands
 * on the downbeat.  Cross-CPU rules (see common/ipc.h):
 *   - the ARM9 only ever moves evo_state IDLE -> READY, and only after the
 *     candidate, its target and its epoch are fully written (uncached stores
 *     drain in order);
 *   - the ARM7 only moves it READY -> IDLE (commit, drop, edit, undo);
 *   - a candidate prepared before an edit of its pattern carries an old
 *     epoch and is refused at the commit point.
 */
#include <string.h>
#include "corrosion.h"
#include "app.h"
#include "audio.h"

#define N_ANCHORS 8
static pattern_t anchors[N_ANCHORS];
static uint8_t anchor_id[N_ANCHORS] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t anchor_next;
static uint16_t evo_rng;
static uint16_t evo_ctr;
static pattern_t evo_src, evo_tmp;
static uint8_t last_commits;

#define BARRIER() __asm__ volatile("" ::: "memory")

void anchors_forget(uint8_t pat)
{
    uint8_t i;
    for (i = 0; i < N_ANCHORS; i++)
        if (pat == 0xFF || anchor_id[i] == pat) anchor_id[i] = 0xFF;
}

static pattern_t *anchor_for(uint8_t pat, const pattern_t *now)
{
    uint8_t i;
    for (i = 0; i < N_ANCHORS; i++)
        if (anchor_id[i] == pat) return &anchors[i];
    i = anchor_next;
    anchor_next = (uint8_t)((anchor_next + 1) & (N_ANCHORS - 1));
    anchor_id[i] = pat;
    memcpy(&anchors[i], now, sizeof(pattern_t));
    return &anchors[i];
}

void evo_seed(uint16_t entropy)
{
    evo_rng = entropy ? entropy : 0x2601;
    last_commits = SH->evo_commits;
}

/* fresh randomness for every evolution: the entropy pool (button timing,
 * the moment START was pressed) stirred with a xorshift */
uint16_t evo_random(void)
{
    evo_rng ^= (uint16_t)(ui_entropy ^ hw_noise());
    evo_rng ^= (uint16_t)(evo_rng << 7);
    evo_rng ^= (uint16_t)(evo_rng >> 9);
    evo_rng ^= (uint16_t)(evo_rng << 8);
    if (!evo_rng) evo_rng = 0x2601;
    return evo_rng;
}

/* After each committed evolution the SOUND drifts too (ETCH): the bass
 * cutoff and each of ETCH's four drum lanes take a small bounded step. */
static void sound_drift(void)
{
    uint8_t l, r;
    int8_t lim = SH->proj.gen.mode == EVO_CORRODE ? 5 : 2;
    int8_t dlim = SH->proj.gen.mode == EVO_CORRODE ? 2 : 1;
    int8_t v;
    if (SH->proj.gen.mode == EVO_STABLE) return;
    r = (uint8_t)evo_random();
    v = (int8_t)(evo_cut_ofs + (int8_t)((r & 3) == 0 ? -1 : (r & 3) == 1 ? 1 : 0));
    if (v > lim) v = lim;
    if (v < -lim) v = -lim;
    evo_cut_ofs = v;
    for (l = 0; l < NUM_LANES; l++) {
        r >>= 2;
        if (l == 2) r = (uint8_t)(evo_random() >> 3);
        v = (int8_t)(evo_tune_ofs[l] + (int8_t)((r & 3) == 0 ? -1 : (r & 3) == 1 ? 1 : 0));
        if (v > dlim) v = dlim;
        if (v < -dlim) v = -dlim;
        evo_tune_ofs[l] = v;
    }
}

static void sound_drift_reset(void)
{
    uint8_t l;
    evo_cut_ofs = 0;
    for (l = 0; l < NUM_LANES; l++) evo_tune_ofs[l] = 0;
}

/* Prepare the next mutation (main loop).  Returns 1 if it did the work. */
uint8_t evo_service(void)
{
    uint8_t mode = SH->proj.gen.mode, target, ep;
    target = SH->playing ? app_next_bar_pattern() : SH->cur;
    if (SH->evo_state == EVO_READY && SH->evo_target != target) {
        app_send(CMD(CMD_EVO_DROP, 0));          /* playback moved on */
        return 0;
    }
    if (SH->evo_state != EVO_IDLE) return 0;
    if (mode == EVO_STABLE && !SH->evo_request) return 0;
    ep = SH->pat_epoch[target];
    BARRIER();
    memcpy(&evo_src, &SH->proj.pat[target], sizeof(pattern_t));
    corr_mutate(&evo_tmp, &evo_src, anchor_for(target, &evo_src), &SH->proj.gen,
                mode == EVO_STABLE ? EVO_DRIFT : mode, SH->proj.gen.mutation,
                (uint16_t)(evo_random() + (++evo_ctr)));
    if (SH->evo_state != EVO_IDLE) return 1;     /* only the ARM9 sets READY */
    memcpy(&SH->evo_cand, &evo_tmp, sizeof(pattern_t));
    SH->evo_target = target;
    SH->evo_epoch = ep;
    BARRIER();
    SH->evo_state = EVO_READY;
    return 1;
}

/* evolve immediately (stopped): mutate pattern pat into out */
uint8_t evo_mutate_now(uint8_t pat, pattern_t *out)
{
    uint8_t mode = SH->proj.gen.mode;
    if (SH->proj.pat[pat].lock & PLOCK_ALL) return 0;
    memcpy(&evo_src, &SH->proj.pat[pat], sizeof(pattern_t));
    corr_mutate(out, &evo_src, anchor_for(pat, &evo_src), &SH->proj.gen,
                mode == EVO_STABLE ? EVO_DRIFT : mode, SH->proj.gen.mutation,
                (uint16_t)(evo_random() + (++evo_ctr)));
    return 1;
}

/* once per frame: react to commits (sound drift), reset drift in STABLE */
uint8_t evo_frame(void)
{
    uint8_t c = SH->evo_commits, committed = 0;
    if (c != last_commits) {
        last_commits = c;
        sound_drift();
        committed = 1;
    }
    if (SH->proj.gen.mode == EVO_STABLE && (evo_cut_ofs || evo_tune_ofs[0] || evo_tune_ofs[1] ||
                                            evo_tune_ofs[2] || evo_tune_ofs[3]))
        sound_drift_reset();
    evo_service();
    return committed;
}
