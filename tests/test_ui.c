/*
 * ETCH CRANK -- by Synth Centric
 * The interface on the host: the real screens, framework, Playdate-only layer
 * and project code over the real engine, driven by scripted button presses
 * and crank turns at 50 frames a second, with the audio callback pulling
 * 44.1 kHz between frames.  Checks that the grammar does what ETCH does, and
 * writes every screen as an image (build/host/ui_*.pbm; tools/screens_png.py
 * turns them into docs/images/).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "audio.h"
#include "app_glue.h"
#include "corrosion.h"
#include "storage.h"
#include "pump.h"
#include "crank.h"
#include "ui.h"
#include "gfx1.h"
#include "cpl.h"
#include "proj.h"
#include "plat.h"
#include "layers.h"
#include "demos.h"

uint16_t hw_noise(void) { return 0x1357; }
double plat_tick_hz(void) { return 1e9; }
double plat_ui_ms(void) { return 1.5; }

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
                           printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t held, docked = 1;
static uint32_t now_ms;

/* one update-loop frame: 20 ms of audio, then the update loop */
static void frame_in(uint8_t pushed, uint8_t released, int32_t crank_q8)
{
    static int16_t l[882], r[882];
    ui_in_t in;
    pump_pull(l, r, 882);
    held = (uint8_t)((held | pushed) & ~released);
    in.held = held;
    in.pushed = pushed;
    in.released = released;
    in.crank_q8 = crank_q8;
    in.docked = docked;
    in.now_ms = now_ms;
    screen_frame(&in);
    cpl_frame(now_ms, 0.0f, 1);
    layers_frame();
    synth_frame();
    evo_frame();
    app_fill_frame();
    now_ms += 20;
}
static void idle(int n) { while (n--) frame_in(0, 0, 0); }
static void press(uint8_t b) { frame_in(b, 0, 0); frame_in(0, b, 0); idle(1); }
static void chord(uint8_t hold, uint8_t b) { frame_in(hold, 0, 0); frame_in(b, 0, 0); frame_in(0, b, 0); frame_in(0, hold, 0); idle(1); }
static void crank(int deg) { int i; for (i = 0; i < 10; i++) frame_in(0, 0, deg * 256 / 10); idle(2); }

static void snap(const char *name)
{
    char p[128];
    FILE *f;
    int y;
    snprintf(p, sizeof p, "ui_%s.pbm", name);
    f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P4\n%d %d\n", GFX_W, GFX_H);
    for (y = 0; y < GFX_H; y++) {
        uint8_t row[GFX_W / 8];
        int i;
        for (i = 0; i < GFX_W / 8; i++) row[i] = (uint8_t)~gfx_fb[y * GFX_STRIDE + i];   /* PBM: 1 = black */
        fwrite(row, sizeof row, 1, f);
    }
    fclose(f);
}

static int first_rest(void)
{
    int s;
    for (s = 0; s < 16; s++) if ((SH->proj.pat[SH->cur].flags[s] & STEP_TYPE_MASK) == STEP_REST) return s;
    return -1;
}

int main(void)
{
    int s, i;
    remove("etch_crank.sav");
    remove("etch_crank_ext.sav");
    audio_engine_init();
    foundry_build();
    shared_init();
    proj_init();
    cpl_init();
    pump_init(SH);
    crank_ui_init(acid_p.cutoff, acid_p.res, 1);
    screen_init();
    evo_seed(0x1357);
    app_send(CMD(CMD_START, 0x1357));
    idle(100);
    CHECK(SH->playing, "plays at launch");
    CHECK(ui_screen() == SCR_LIVE, "LIVE is home");
    snap("live");

    /* LIVE: A on an empty bass step writes the last note; A again removes it */
    s = first_rest();
    CHECK(s >= 0, "the boot pattern has a rest");
    for (i = 0; i < s; i++) press(BTN_RIGHT);
    press(BTN_A);
    idle(3);
    CHECK((SH->proj.pat[SH->cur].flags[s] & STEP_TYPE_MASK) == STEP_NOTE, "A writes a note on step %d", s + 1);
    chord(BTN_A, BTN_RIGHT);                     /* A + RIGHT: a semitone up */
    idle(3);
    CHECK(SH->proj.pat[SH->cur].note[s] == 25, "A + RIGHT: semitone (%u)", SH->proj.pat[SH->cur].note[s]);
    chord(BTN_B, BTN_A);                         /* B + A deletes */
    idle(3);
    CHECK((SH->proj.pat[SH->cur].flags[s] & STEP_TYPE_MASK) == STEP_REST, "B + A deletes");
    /* a drum hit */
    press(BTN_DOWN);
    {
        uint16_t before = SH->proj.pat[SH->cur].drum[0];
        press(BTN_A);
        idle(3);
        CHECK(SH->proj.pat[SH->cur].drum[0] == (uint16_t)(before ^ (1u << s)), "A toggles a kick");
    }
    /* B tap: FILL latch */
    press(BTN_B);
    CHECK(ui_fill_on, "B tap on LIVE: FILL on");
    press(BTN_B);
    CHECK(!ui_fill_on, "B tap again: FILL off");
    /* B + DOWN: play / stop */
    chord(BTN_B, BTN_DOWN);
    idle(5);
    CHECK(!SH->playing, "B + DOWN stops");
    chord(BTN_B, BTN_DOWN);
    idle(5);
    CHECK(SH->playing, "B + DOWN plays");
    CHECK(!ui_fill_on, "a B chord is not a B tap");

    /* the crank on cutoff, B + crank on resonance, docking home */
    docked = 0;
    idle(2);
    crank(90);
    idle(10);
    CHECK(crank_cut_offset_q8() > 20 * 256, "crank opens the filter (%d)", crank_cut_offset_q8() >> 8);
    frame_in(BTN_B, 0, 0);
    crank(-45);
    frame_in(0, BTN_B, 0);
    idle(2);
    CHECK(crank_ui.res_q8 < (92 << 8), "B + crank lowers resonance");
    CHECK(!ui_fill_on, "B + crank is not a B tap");
    snap("live_crank");
    docked = 1;
    idle(40);
    CHECK(crank_ui.cut_q8 == crank_ui.stored_cut << 8, "docked: home after a beat");

    /* the screen row: B + LEFT / RIGHT, wrapping */
    chord(BTN_B, BTN_LEFT);
    CHECK(ui_screen() == SCR_TRACK, "B + LEFT: TRACK");
    snap("track");
    chord(BTN_B, BTN_LEFT);
    CHECK(ui_screen() == SCR_SYNTH, "B + LEFT: SYNTH");
    snap("synth");
    chord(BTN_B, BTN_LEFT);
    CHECK(ui_screen() == SCR_FILE, "B + LEFT from SYNTH wraps to FILE");
    chord(BTN_B, BTN_RIGHT);
    CHECK(ui_screen() == SCR_SYNTH, "B + RIGHT back to SYNTH");

    /* SYNTH: the BASS page, CUTOFF row: RIGHT +1 (the stored value) */
    press(BTN_DOWN);                         /* the list opens on WAVE: CUTOFF */
    {
        uint8_t c0 = crank_ui.stored_cut;
        press(BTN_RIGHT);
        CHECK(crank_ui.stored_cut == c0 + 1, "SYNTH CUTOFF +1 moves the stored value (%u -> %u)", c0, crank_ui.stored_cut);
        chord(BTN_B, BTN_A);
        CHECK(crank_ui.stored_cut == 30, "B + A: back to default");
    }
    /* A held + crank rides the value */
    press(BTN_DOWN); press(BTN_DOWN);        /* ENV MOD */
    {
        uint8_t e0 = base_acid.envmod;
        docked = 0;
        frame_in(BTN_A, 0, 0);
        crank(60);
        frame_in(0, BTN_A, 0);
        idle(2);
        CHECK(base_acid.envmod > e0, "A + crank rides ENV MOD (%u -> %u)", e0, base_acid.envmod);
        CHECK(acid_p.envmod == base_acid.envmod, "the engine follows");
        CHECK(crank_cut_offset_q8() == 0, "the crank did not move the filter meanwhile");
        docked = 1;
        idle(30);
    }
    /* drums: KICK tab, PITCH, re-synthesis while playing */
    for (i = 0; i < 5; i++) press(BTN_UP);  /* up past WAVE: the tab row */
    press(BTN_RIGHT);                        /* KICK */
    snap("synth_kick");
    press(BTN_DOWN); press(BTN_DOWN); press(BTN_DOWN);   /* PITCH */
    {
        uint16_t f0 = foundry_defs[0][SH->proj.pat[SH->cur].kit[0] & 3].f0;
        press(BTN_RIGHT);
        idle(15);
        CHECK(foundry_defs[0][SH->proj.pat[SH->cur].kit[0] & 3].f0 != f0, "KICK PITCH edits the design");
    }
    for (i = 0; i < 5; i++) press(BTN_UP);
    for (i = 0; i < 4; i++) press(BTN_RIGHT);     /* FX */
    snap("synth_fx");
    press(BTN_RIGHT);                             /* MOD */
    snap("synth_mod");
    press(BTN_RIGHT);                             /* CPU */
    snap("synth_cpu");

    /* GEN: GEN ALL changes the pattern */
    chord(BTN_B, BTN_RIGHT); chord(BTN_B, BTN_RIGHT); chord(BTN_B, BTN_RIGHT);
    CHECK(ui_screen() == SCR_GEN, "GEN");
    snap("gen");
    for (i = 0; i < 2; i++) press(BTN_RIGHT);      /* KEY + 2 */
    for (i = 0; i < 12; i++) press(BTN_DOWN);      /* to the actions */
    {
        pattern_t before;
        memcpy(&before, &SH->proj.pat[SH->cur], sizeof before);
        press(BTN_A);                              /* GEN ALL */
        idle(5);
        CHECK(memcmp(&before, &SH->proj.pat[SH->cur], sizeof before) != 0, "GEN ALL writes a new groove");
    }
    snap("gen_actions");

    /* SONG: BPM */
    chord(BTN_B, BTN_RIGHT);
    CHECK(ui_screen() == SCR_SONG, "SONG");
    press(BTN_DOWN);
    {
        uint8_t b0 = SH->proj.synth.bpm;
        press(BTN_RIGHT);
        idle(2);
        CHECK(SH->proj.synth.bpm == b0 + 1 && fx_p.bpm == b0 + 1, "BPM +1");
    }
    snap("song");

    /* FILE: save slot 1, load a demo, load the slot back */
    chord(BTN_B, BTN_RIGHT);
    CHECK(ui_screen() == SCR_FILE, "FILE");
    snap("file");
    press(BTN_DOWN); press(BTN_DOWN);              /* SAVE */
    press(BTN_A);
    snap("file_save");
    press(BTN_A);                                  /* slot 1 */
    idle(5);
    {
        char nm[11];
        CHECK(storage_slot_status(0, nm) == SLOT_OK, "slot 1 saved, verified by ETCH DS's storage code");
    }
    press(BTN_B);                                  /* back to the menu */
    press(BTN_UP);                                 /* LOAD */
    press(BTN_A);
    press(BTN_DOWN);                               /* D2 */
    press(BTN_A); press(BTN_A);
    idle(20);
    CHECK(!memcmp(SH->proj.name, "SLAG", 4), "demo D2 is ETCH's SLAG");
    CHECK(SH->playing && ui_screen() == SCR_LIVE, "loading goes to LIVE and plays");
    snap("live_slag");
    chord(BTN_B, BTN_LEFT);                        /* LIVE -> FILE (wrap) */
    chord(BTN_B, BTN_LEFT); chord(BTN_B, BTN_LEFT); chord(BTN_B, BTN_LEFT);
    chord(BTN_B, BTN_LEFT); chord(BTN_B, BTN_LEFT);
    CHECK(ui_screen() == SCR_LIVE, "six screens round the row");
    for (i = 0; i < 3; i++) chord(BTN_B, BTN_RIGHT);
    CHECK(ui_screen() == SCR_FILE, "FILE again");
    press(BTN_A);                                  /* LOAD (FILE keeps its place) */
    for (i = 0; i < DEMO_COUNT + 1; i++) press(BTN_DOWN);   /* S01 */
    snap("file_load");
    press(BTN_A); press(BTN_A);
    idle(20);
    CHECK(!memcmp(SH->proj.name, "CORRODE", 7), "slot 1 loads back (CORRODE)");

    /* crank recording: B + UP arms, the next bar records, then it plays */
    docked = 0;
    chord(BTN_B, BTN_UP);
    CHECK(cpl_rec_state() == 1, "B + UP arms");
    for (i = 0; i < 260 && cpl_rec_state() != 2; i++) idle(1);
    CHECK(cpl_rec_state() == 2, "recording from the bar line");
    for (i = 0; i < 100 && cpl_rec_state() == 2; i++) frame_in(0, 0, (i < 50 ? 4 : -4) * 256);
    for (i = 0; i < 200 && cpl_rec_state() == 2; i++) idle(1);
    CHECK(cpl_rec_state() == 0 && cpl_motion_playing(), "one pass recorded, playing back");
    snap("live_lane");

    /* a scene: store, then morph by A + crank */
    chord(BTN_B, BTN_LEFT); chord(BTN_B, BTN_RIGHT);    /* re-enter LIVE */
    for (i = 0; i < 6; i++) press(BTN_UP);                /* BASS (LIVE keeps its place) */
    for (i = 0; i < 5; i++) press(BTN_DOWN);              /* pads */
    for (i = 0; i < 4; i++) press(BTN_RIGHT);             /* S1 */
    press(BTN_A);
    CHECK(cplp.scene[0].used, "A on an empty scene stores it");
    base_acid.envmod = 10;
    frame_in(BTN_A, 0, 0);
    crank(180);
    frame_in(0, BTN_A, 0);
    idle(2);
    CHECK(base_acid.envmod == cplp.scene[0].acid.envmod, "A + crank morphs all the way to the scene (%u vs %u)",
          base_acid.envmod, cplp.scene[0].acid.envmod);
    snap("live_pads");

    printf("test_ui: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
