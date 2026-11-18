/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * Projects: new, ETCH's demo songs, save and load, imports.
 *
 * The project is ETCH's project_t with ETCH DS's sound settings, saved by ETCH
 * DS's own storage code (engine/storage.c, unmodified) in ETCH DS's slot
 * format: etch_crank.sav in the game's data folder.  ETCH DS saves load as
 * they are (etch_ds.sav, copied into the data folder), and ETCH Game Boy saves
 * import (etch_gb.sav).  The Playdate-only data goes in the sidecar
 * (cpl.c, etch_crank_ext.sav), one slot per project slot.
 *
 * Loading works as on ETCH DS (edit.c's apply_project): stop, let the audio
 * go quiet, swap the project in, re-synthesise the drums, new edit epochs,
 * restart.  ETCH: loading jumps to LIVE and plays.
 */
#include <string.h>
#include "proj.h"
#include "app_glue.h"
#include "audio.h"
#include "corrosion.h"
#include "storage.h"
#include "crank.h"
#include "cpl.h"
#include "demos.h"
#include "ui.h"
#include "layers.h"

static const char *const own_path = "etch_crank.sav";

void proj_init(void)
{
    storage_path = own_path;
    storage_gb_path = "etch_gb.sav";
}

/* the same steps as ETCH DS's apply_project, for projects that do not come
 * from a save slot (new, demo songs, codes) */
static void apply(const project_t *p, const ds_ext_t *x)
{
    uint32_t spin = 0, t;
    app_send(CMD(CMD_STOP, 0));
    while (SH->playing && ++spin < 40000000u) { }
    t = SH->blk_played;
    spin = 0;
    while (SH->blk_played < t + 3 && ++spin < 40000000u) { }
    memcpy(&SH->proj, p, sizeof *p);
    if (x) {
        acid_p = x->acid;
        fx_p = x->fx;
        memcpy(foundry_tune, x->drum_tune, DS_LANES);
        memcpy(foundry_decay, x->drum_decay, DS_LANES);
        memcpy(foundry_level, x->drum_level, DS_LANES);
        if (x->has_drums) memcpy(foundry_defs, x->drums, sizeof x->drums);
        else memcpy(foundry_defs, foundry_presets, sizeof foundry_defs);
    } else {
        uint8_t l;
        demo_import_sound(p);                       /* ETCH's coarse sound -> ACID */
        for (l = 0; l < NUM_LANES; l++) {
            foundry_tune[l] = p->drum_tune[l] & 15;
            foundry_decay[l] = p->drum_decay[l] & 15;
            foundry_level[l] = p->drum_level[l] & 15;
        }
        memcpy(foundry_defs, foundry_presets, sizeof foundry_defs);
    }
    foundry_build();
    fx_p.bpm = p->synth.bpm;
    anchors_forget(0xFF);
    app_send(CMD(CMD_EDITED, 0xFF));
    app_send(CMD(CMD_TEMPO, 0));
    app_send(CMD(CMD_QUEUE, p->start_pattern < NUM_PATTERNS ? p->start_pattern : 0));
    {
        uint16_t e = (uint16_t)(ui_entropy ^ hw_noise());
        evo_seed(e);
        app_send(CMD(CMD_START, e));
    }
}

/* after any load: the crank takes the new stored values, the sound's base */
static void settle(void)
{
    crank_ui_reload(acid_p.cutoff, acid_p.res);
    cpl_sync_base();
    layers_project_loaded();
    ui_fill_on = 0;
    ui_goto(SCR_LIVE);
}

void proj_new(void)
{
    static project_t p;
    uint8_t i;
    memset(&p, 0, sizeof p);
    memcpy(p.name, "UNTITLED  ", PROJECT_NAME_LEN);
    demo_load(&p);                                  /* CORRODE's sound and settings ... */
    memcpy(p.name, "UNTITLED  ", PROJECT_NAME_LEN);
    for (i = 0; i < NUM_PATTERNS; i++) pattern_clear(&p.pat[i]);   /* ... and nothing written */
    p.gen.seed = (uint16_t)(ui_entropy ^ hw_noise());
    apply(&p, NULL);
    cpl_new_project();
    settle();
    ui_message("NEW PROJECT: GEN WRITES THE FIRST GROOVE");
}

void proj_demo(uint8_t d)
{
    static project_t p;
    demos_load(d, &p);
    apply(&p, NULL);
    cpl_new_project();
    settle();
    ui_message("DEMO %u: %s", d + 1, demo_names[d & 7]);
}

void proj_boot_song(void)
{
    static project_t p;
    demo_load(&p);
    apply(&p, NULL);
    cpl_new_project();
    settle();
    ui_message("CORRODE FROM ITS CODE 0F1C-9009-5870");
}

uint8_t proj_save(uint8_t slot)
{
    uint8_t ok;
    cpl_before_save();
    ok = app_save_slot(slot);
    if (ok) ok = cpl_save_slot(slot);
    ui_message(ok ? "SAVED TO SLOT %02u" : "SAVE FAILED (SLOT %02u)", slot + 1);
    return ok;
}

uint8_t proj_load(uint8_t slot)
{
    if (!app_load_slot(slot)) { ui_message("SLOT %02u: NOTHING TO LOAD", slot + 1); return 0; }
    if (!cpl_load_slot(slot)) cpl_new_project();
    settle();
    ui_message("LOADED SLOT %02u", slot + 1);
    return 1;
}

uint8_t proj_erase(uint8_t slot)
{
    uint8_t ok = storage_erase(slot);
    ui_message(ok ? "SLOT %02u ERASED" : "ERASE FAILED", slot + 1);
    return ok;
}

uint8_t proj_import_ds(uint8_t slot)
{
    uint8_t ok;
    storage_path = "etch_ds.sav";
    ok = app_load_slot(slot);
    storage_path = own_path;
    if (!ok) { ui_message("ETCH DS SLOT %02u: NOTHING TO LOAD", slot + 1); return 0; }
    cpl_new_project();
    settle();
    ui_message("ETCH DS SLOT %02u IMPORTED", slot + 1);
    return 1;
}

uint8_t proj_import_gb(uint8_t slot)
{
    if (!app_import_gb(slot)) { ui_message("ETCH GB SLOT %u: NOTHING TO IMPORT", slot + 1); return 0; }
    cpl_new_project();
    settle();
    ui_message("ETCH GAME BOY SLOT %u IMPORTED", slot + 1);
    return 1;
}

uint8_t proj_slot_status(uint8_t which, uint8_t slot, char *name10)
{
    uint8_t s;
    if (which == 1) {
        storage_path = "etch_ds.sav";
        s = storage_slot_status(slot, name10);
        storage_path = own_path;
        return s;
    }
    if (which == 2) return storage_gb_status(slot, name10);
    return storage_slot_status(slot, name10);
}
