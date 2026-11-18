/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * GEN: CORROSION.  ETCH's GEN screen:
 *
 *   KEY ... MODE   LEFT / RIGHT or A + D-pad
 *   SEED           A + LEFT / RIGHT +-1, A + UP / DOWN +-256; A tap: a new
 *                  random seed and GEN ALL; B + A: the seed back to 0000
 *   CODE           the seed code: 12 digits (CORROSION's, every ETCH) and the
 *                  optional 4th group (the series' expansion seed).  LEFT /
 *                  RIGHT picks a digit, A + UP / DOWN changes it, A tap loads
 *                  the code (its settings, then GEN ALL); B + A clears the
 *                  4th group
 *   actions        LEFT / RIGHT picks, A runs: GEN ALL . GEN BASS . GEN DRUMS .
 *                  GROOVE . EVOLVE . UNDO . LOCK BASS . LOCK PATT
 */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "gfx1.h"
#include "app_glue.h"
#include "corrosion.h"
#include "cpl.h"

static const char *const key_names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
#define GG(f) static int g_##f(void) { return SH->proj.gen.f; } static void s_##f(int v) { SH->proj.gen.f = (uint8_t)v; }
GG(key) GG(scale) GG(style) GG(density) GG(sync) GG(accent) GG(slide) GG(octave) GG(mutation) GG(mode)
static int g_seed(void) { return SH->proj.gen.seed; }
static void s_seed(int v) { SH->proj.gen.seed = (uint16_t)v; }
static void f_key(int v, char *o) { strcpy(o, key_names[v % 12]); }
static void f_scale(int v, char *o) { strcpy(o, corr_scale_names[v % SCALE_COUNT]); }
static void f_style(int v, char *o) { strcpy(o, corr_style_names[v % STYLE_COUNT]); }
static void f_mode(int v, char *o) { strcpy(o, corr_mode_names[v % EVO_COUNT]); }
static void f_seed(int v, char *o) { sprintf(o, "%04X", v & 0xFFFF); }
static void f_oct(int v, char *o) { static const char *const n[3] = { "LOW", "MID", "HIGH" }; strcpy(o, n[v % 3]); }
static void a_newseed(void)
{
    SH->proj.gen.seed = (uint16_t)(evo_random() ^ hw_noise());
    app_generate(GEN_ALL);
    ui_message("NEW SEED %04X: GEN ALL", SH->proj.gen.seed);
}
static const row_t gen_rows[] = {
    { "KEY", g_key, s_key, 0, 11, 7, 9, f_key, NULL },
    { "SCALE", g_scale, s_scale, 0, SCALE_COUNT - 1, 1, 0, f_scale, NULL },
    { "STYLE", g_style, s_style, 0, STYLE_COUNT - 1, 1, 0, f_style, NULL },
    { "DENSITY", g_density, s_density, 0, 15, 4, 9, NULL, NULL },
    { "SYNCOPATION", g_sync, s_sync, 0, 15, 4, 5, NULL, NULL },
    { "ACCENT", g_accent, s_accent, 0, 15, 4, 8, NULL, NULL },
    { "SLIDE", g_slide, s_slide, 0, 15, 4, 7, NULL, NULL },
    { "OCTAVE", g_octave, s_octave, 0, 2, 1, 0, f_oct, NULL },
    { "MUTATE", g_mutation, s_mutation, 0, 15, 4, 5, NULL, NULL },
    { "MODE", g_mode, s_mode, 0, EVO_COUNT - 1, 1, EVO_DRIFT, f_mode, NULL },
    { "SEED", g_seed, s_seed, 0, 0xFFFF, 256, 0, f_seed, a_newseed },
};
#define N_ROWS (int)(sizeof gen_rows / sizeof gen_rows[0])
static list_t glist = { gen_rows, sizeof gen_rows / sizeof gen_rows[0], 0, 0, 0 };

enum { A_LIST, A_CODE, A_ACT };
static uint8_t area = A_LIST, digit, act;
static char code[17];                       /* 16 digits being edited */
static uint8_t code_dirty;
static const char *const act_names[8] = { "GEN ALL", "GEN BASS", "GEN DRUMS", "GROOVE", "EVOLVE", "UNDO", "LOCK BASS", "LOCK PATT" };

static void code_from_project(void)
{
    corr_code_encode(&SH->proj.gen, code);
    memcpy(code + 12, cplp.has_ext ? cplp.ext : "----", 4);
    code[16] = 0;
    code_dirty = 0;
}

static void code_load(void)
{
    gen_t g = SH->proj.gen;
    if (!corr_code_decode(&g, code)) { ui_message("THAT CODE IS NOT VALID"); return; }
    g.mutation = SH->proj.gen.mutation;
    g.mode = SH->proj.gen.mode;
    SH->proj.gen = g;
    if (code[12] != '-') { memcpy(cplp.ext, code + 12, 4); cplp.has_ext = 1; }
    else cplp.has_ext = 0;
    app_generate(GEN_ALL);
    code_dirty = 0;
    ui_message("CODE LOADED: GEN ALL");
}

static void act_do(int i)
{
    pattern_t p;
    switch (i) {
    case 0: app_generate(GEN_ALL); ui_message("GEN ALL"); break;
    case 1: app_generate(GEN_BASS); ui_message("GEN BASS"); break;
    case 2: app_generate(GEN_DRUMS); ui_message("GEN DRUMS"); break;
    case 3:
        memcpy(&p, &SH->proj.pat[SH->cur], sizeof p);
        corr_drum_preset(&p, SH->proj.gen.style);
        app_replace(&p, 1);
        ui_message("GROOVE: %s", corr_style_names[SH->proj.gen.style % STYLE_COUNT]);
        break;
    case 4: app_evolve(); ui_message("EVOLVE AT THE NEXT BAR"); break;
    case 5: app_undo(); ui_message("UNDO"); break;
    case 6: case 7:
        memcpy(&p, &SH->proj.pat[SH->cur], sizeof p);
        p.lock ^= i == 6 ? PLOCK_BASS : PLOCK_ALL;
        app_replace(&p, 0);
        ui_message("%s %s", i == 6 ? "BASS" : "PATTERN", (p.lock & (i == 6 ? PLOCK_BASS : PLOCK_ALL)) ? "LOCKED" : "UNLOCKED");
        break;
    }
}

static int hexval(char c) { return c >= 'A' ? c - 'A' + 10 : c >= '0' && c <= '9' ? c - '0' : -1; }

static void gen_event(const ui_ev_t *e)
{
    if (area == A_LIST) {
        if (e->nav_y > 0 && glist.sel == N_ROWS - 1) { area = A_CODE; if (!code_dirty) code_from_project(); return; }
        list_event(&glist, e);
        return;
    }
    if (area == A_CODE) {
        if (e->nav_y < 0) { area = A_LIST; return; }
        if (e->nav_y > 0) { area = A_ACT; return; }
        if (e->nav_x) digit = (uint8_t)((digit + e->nav_x + 16) % 16);
        if (e->adj_y || e->adj_x) {
            int v = hexval(code[digit]), d = e->adj_x - e->adj_y;
            if (digit >= 12 && v < 0) v = d > 0 ? -1 : 16;
            v += d;
            if (digit >= 12) {                          /* the expansion group: '-' or hex */
                if (v < 0 || v > 15) { memcpy(code + 12, "----", 4); }
                else {
                    int k;
                    for (k = 12; k < 16; k++) if (code[k] == '-') code[k] = '0';
                    code[digit] = "0123456789ABCDEF"[v];
                }
            } else {
                static const uint8_t hi[12] = { 15, 15, 15, 15, 11, 5, 5, 15, 15, 15, 15, 2 };
                v = v < 0 ? hi[digit] : v > hi[digit] ? 0 : v;
                code[digit] = "0123456789ABCDEF"[v];
            }
            code_dirty = 1;
        }
        if (e->del) { memcpy(code + 12, "----", 4); code_dirty = 1; }
        if (e->a_tap) code_load();
        return;
    }
    if (e->nav_y < 0) { if (act >= 2) act = (uint8_t)(act - 2); else area = A_CODE; return; }
    if (e->nav_y > 0 && act < 6) act = (uint8_t)(act + 2);
    if (e->nav_x) act = (uint8_t)((act ^ 1));
    if (e->a_tap) act_do(act);
}

static uint8_t gen_can_ride(void) { return area == A_LIST && list_can_ride(&glist); }

static void gen_draw(void)
{
    int i, x;
    char b[24];
    list_draw(&glist, 8, 70, 222, 12, N_ROWS);
    gfx_vline(240, 66, 158, C_D50);
    /* the code */
    if (!code_dirty) code_from_project();
    gfx_text(248, 70, F_SMALL, 1, C_BLACK, code_dirty ? "CODE (A LOADS IT)" : "SEED CODE");
    x = 248;
    for (i = 0; i < 16; i++) {
        int sel = area == A_CODE && digit == i;
        b[0] = code[i]; b[1] = 0;
        if (i && (i & 3) == 0) x += 4;
        if (sel) gfx_fill(x - 1, 78, 9, 11, C_BLACK);
        gfx_text(x, 80, F_MAIN, 1, sel ? C_WHITE : (i >= 12 ? C_BLACK : C_BLACK), b);
        if (i >= 12) gfx_hline(x, 90, 6, C_D50);
        x += 8;
    }
    if (area == A_CODE) gfx_frame(246, 76, 150, 15, C_BLACK);
    gfx_text(248, 94, F_SMALL, 1, C_BLACK, "SEED KEY SCALE STYLE DEN SYNC ACC SLD OCT  + EXP");
    /* the actions */
    for (i = 0; i < 8; i++) {
        int ax = 248 + (i & 1) * 74, ay = 106 + (i >> 1) * 22, sel = area == A_ACT && act == i;
        int lit = (i == 6 && (SH->proj.pat[SH->cur].lock & PLOCK_BASS)) || (i == 7 && (SH->proj.pat[SH->cur].lock & PLOCK_ALL));
        gfx_fill(ax, ay, 70, 18, lit ? C_BLACK : C_WHITE);
        gfx_frame(ax, ay, 70, 18, C_BLACK);
        gfx_text_c(ax + 35, ay + 6, F_MAIN, 1, lit ? C_WHITE : C_BLACK, act_names[i]);
        if (sel) gfx_frame(ax - 2, ay - 2, 74, 22, C_BLACK);
    }
    gfx_textf(248, 196, F_MAIN, 1, C_BLACK, "CORROSION FROM ETCH %s", ETCH_VERSION_STR);
    gfx_textf(248, 207, F_SMALL, 1, C_BLACK, "PATTERN %02u  GENERATION %u", SH->cur + 1, SH->proj.pat[SH->cur].gen);
}

static const char *gen_hint(void)
{
    if (area == A_CODE) return "L/R DIGIT   A+U/D CHANGE   A LOAD   B+A NO 4TH GROUP";
    if (area == A_ACT) return "D-PAD CHOOSE   A RUN";
    if (glist.sel == N_ROWS - 1) return "A NEW SEED + GEN ALL   A+U/D +-256   B+A 0000";
    return "L/R OR A+D-PAD CHANGE   A+CRANK RIDE   DOWN: CODE, ACTIONS";
}

static void gen_enter(void) { if (!code_dirty) code_from_project(); }

const screen_t scr_gen = { "GEN", gen_event, gen_draw, gen_can_ride, gen_hint, gen_enter };
