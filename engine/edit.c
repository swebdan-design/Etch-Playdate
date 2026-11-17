/*
 * ETCH DS -- by Synth Centric
 * Editing and performance actions (ETCH's app_* functions), for the GUI.
 *
 * Every change to pattern memory goes to the ARM7 as a command, so the ARM7
 * stays the only writer of the playing pattern and an edit can never race a
 * CORROSION commit.  Step edits are single-field commands; whole-pattern
 * operations (GEN, REGEN, evolve-while-stopped) go through the edit_pat
 * staging buffer with an acknowledgement.  As in ETCH, an edit makes the
 * edited pattern DRIFT's new anchor.
 */
#include <string.h>
#include "corrosion.h"
#include "app.h"
#include "audio.h"
#include "storage.h"

static uint32_t sent_replace;

void app_set_bass(uint8_t step, uint8_t note, uint8_t flags)
{
    uint8_t pat = SH->cur;
    if (note > NOTE_MAX) note = NOTE_MAX;
    app_send(CMD(CMD_SET_BASS, CMD_BASS_ARG(pat, step & 15, note, flags)));
    anchors_forget(pat);
}

void app_toggle_drum(uint8_t lane, uint8_t step)
{
    uint8_t pat = SH->cur, on = !((SH->proj.pat[pat].drum[lane & 3] >> (step & 15)) & 1);
    app_send(CMD(CMD_SET_DRUM, CMD_DRUM_ARG(pat, lane & 3, step & 15, on)));
    anchors_forget(pat);
}

void app_replace(const pattern_t *p, uint8_t undo)
{
    app_replace_at(SH->cur, p, undo);
}

void app_replace_at(uint8_t pat, const pattern_t *p, uint8_t undo)
{
    uint32_t spin = 0;
    /* the previous replace must have been taken before edit_pat is reused */
    while (SH->edit_ack != sent_replace && ++spin < 2000000u) { }
    memcpy(&SH->edit_pat, p, sizeof(pattern_t));
    sent_replace++;
    app_send(CMD(CMD_REPLACE, pat | (undo ? 0x100u : 0u)));
    anchors_forget(pat);
}

void app_generate(uint8_t what)
{
    pattern_t p;
    memcpy(&p, &SH->proj.pat[SH->cur], sizeof p);
    if (p.lock & PLOCK_ALL) return;
    corr_generate(&p, &SH->proj.gen, what);
    app_replace(&p, 1);
}

uint16_t app_regen(uint8_t what)
{
    pattern_t p, mat;
    uint16_t mask;
    memcpy(&p, &SH->proj.pat[SH->cur], sizeof p);
    if (p.lock & PLOCK_ALL) return 0;
    corr_regen_material(&mat, &SH->proj.gen, evo_random());
    mask = corr_regen_apply(&p, &mat, evo_random(), what);
    if (mask) app_replace(&p, 1);
    return mask;
}

static void fill_masks(void)
{
    uint16_t m[NUM_LANES];
    corr_fill(&SH->proj.pat[SH->cur], evo_random(), m);
    memcpy(SH->fill_req, m, sizeof m);
}

void app_fill(void)
{
    fill_masks();
    app_send(CMD(CMD_FILL, FILL_ONESHOT));
}

static uint8_t latch_bar, latch_pat;

void app_fill_latch(uint8_t on)
{
    if (on) {
        fill_masks();
        latch_bar = SH->bar_total;
        latch_pat = SH->cur;
        app_send(CMD(CMD_FILL, FILL_LATCH_ON));
    } else {
        app_send(CMD(CMD_FILL, FILL_LATCH_OFF));
    }
}

/* a latched fill plays a fresh variation every bar (ETCH) */
void app_fill_frame(void)
{
    if (SH->fill_hold && (SH->bar_total != latch_bar || SH->cur != latch_pat)) {
        latch_bar = SH->bar_total;
        latch_pat = SH->cur;
        fill_masks();
        app_send(CMD(CMD_FILL, FILL_UPDATE));
    }
}

void app_evolve(void)
{
    if (SH->playing) { app_send(CMD(CMD_EVOLVE, 0)); return; }
    {   /* stopped: mutate and apply now (ETCH) */
        pattern_t out;
        if (evo_mutate_now(SH->cur, &out)) app_replace(&out, 1);
    }
}

void app_undo(void)
{
    app_send(CMD(CMD_UNDO, 0));
    anchors_forget(SH->cur);
}

void app_toggle_mute(uint8_t bit)
{
    app_send(CMD(CMD_MUTES, SH->proj.mutes ^ (1u << bit)));
}

void app_queue(uint8_t pat) { app_send(CMD(CMD_QUEUE, pat)); }
void app_chain(uint8_t pos, uint8_t pat, uint8_t rep)
{
    app_send(CMD(CMD_CHAIN, ((uint32_t)pos << 16) | ((uint32_t)pat << 8) | rep));
}
void app_song(uint8_t on) { app_send(CMD(CMD_SONG, on)); }
void app_jump(uint8_t pos) { app_send(CMD(CMD_JUMP, pos)); }

uint8_t app_pattern_empty(uint8_t pat)
{
    const pattern_t *p = &SH->proj.pat[pat & 31];
    uint8_t l;
    for (l = 0; l < NUM_LANES; l++) if (p->drum[l]) return 0;
    return pattern_note_count(p) == 0;
}

/* ---- FILE: save / load / import ------------------------------------------- */
static void snapshot_project(project_t *out)
{
    project_t again;
    int tries;
    for (tries = 0; tries < 4; tries++) {          /* a commit may land mid-copy */
        memcpy(out, &SH->proj, sizeof *out);
        memcpy(&again, &SH->proj, sizeof again);
        if (!memcmp(out, &again, sizeof again)) return;
    }
}

uint8_t app_save_slot(uint8_t slot)
{
    project_t p;
    ds_ext_t x;
    memset(&x, 0, sizeof x);
    snapshot_project(&p);
    x.acid = acid_p;
    x.fx = fx_p;
    memcpy(x.drums, foundry_defs, sizeof x.drums);
    x.has_drums = 1;
    memcpy(x.drum_tune, foundry_tune, DS_LANES);
    memcpy(x.drum_decay, foundry_decay, DS_LANES);
    memcpy(x.drum_level, foundry_level, DS_LANES);
    return storage_save(slot, &p, &x);
}

/* Replace the working project: stop, let the ARM7 go quiet, swap, restart
 * (ETCH: loading jumps to LIVE and plays). */
static void apply_project(const project_t *p, const ds_ext_t *x)
{
    uint32_t spin = 0, t;
    app_send(CMD(CMD_STOP, 0));
    while (SH->playing && ++spin < 4000000u) { }
    t = SH->blk_played;
    spin = 0;
    while (SH->blk_played < t + 3 && ++spin < 4000000u) { }
    memcpy(&SH->proj, p, sizeof *p);
    if (x) {
        acid_p = x->acid;
        fx_p = x->fx;
        memcpy(foundry_tune, x->drum_tune, DS_LANES);
        memcpy(foundry_decay, x->drum_decay, DS_LANES);
        memcpy(foundry_level, x->drum_level, DS_LANES);
        if (x->has_drums) memcpy(foundry_defs, x->drums, sizeof x->drums);   /* save v2 */
        else memcpy(foundry_defs, foundry_presets, sizeof foundry_defs);
    } else {                                         /* ETCH GB: coarse -> ACID */
        uint8_t l;
        demo_import_sound(p);
        for (l = 0; l < NUM_LANES; l++) {            /* ETCH's per-lane voicing */
            foundry_tune[l] = p->drum_tune[l] & 15;
            foundry_decay[l] = p->drum_decay[l] & 15;
            foundry_level[l] = p->drum_level[l] & 15;
        }
        memcpy(foundry_defs, foundry_presets, sizeof foundry_defs);
    }
    foundry_build();                                 /* playback is stopped */
    fx_p.bpm = p->synth.bpm;
    anchors_forget(0xFF);
    app_send(CMD(CMD_EDITED, 0xFF));                 /* new epochs, no undo */
    app_send(CMD(CMD_TEMPO, 0));
    app_send(CMD(CMD_QUEUE, p->start_pattern < NUM_PATTERNS ? p->start_pattern : 0));
    {
        uint16_t e = (uint16_t)(ui_entropy ^ hw_noise());
        evo_seed(e);
        app_send(CMD(CMD_START, e));
    }
}

uint8_t app_load_slot(uint8_t slot)
{
    static project_t p;
    ds_ext_t x;
    if (!storage_load(slot, &p, &x)) return 0;
    apply_project(&p, &x);
    return 1;
}

uint8_t app_import_gb(uint8_t slot)
{
    static project_t p;
    if (!storage_gb_load(slot, &p)) return 0;
    apply_project(&p, NULL);
    return 1;
}

void app_set_kit(uint8_t lane, uint8_t kit)
{
    app_send(CMD(CMD_SET_KIT, ((uint32_t)SH->cur << 4) | ((uint32_t)(lane & 3) << 2) | (kit & 3)));
    anchors_forget(SH->cur);
}

void app_audition(uint8_t lane, uint8_t kit)
{
    live_note(EV_DRUMS, (uint8_t)(1u << lane), (uint8_t)((kit & 3) << ((lane & 3) * 2)));
}
