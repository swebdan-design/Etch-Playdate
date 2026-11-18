/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * SYNTH: the sound of each channel, one page per tab:
 *
 *   BASS   ACID: WAVE CUTOFF RESO ENV MOD DECAY ACCENT SLIDE DRIVE DRV AMT
 *          FILTER (303 / KEY) TUNE LEVEL GATE, LANE (the crank lane)
 *   KICK SNARE HAT OPEN   FOUNDRY, as ETCH DS's DRUM page: KIT MODEL PITCH
 *          PITCH END SWEEP DECAY NOISE DRIVE TUNE LEVEL MUTE LOCK; an edit
 *          re-synthesises the sound while the song plays
 *   FX     DRIVE CRUSH HOLD DELAY (16ths) FEEDBACK DLY MIX ROT COMB NOTE COMB
 *          FB COMB MIX LIMIT
 *   MOD    the four-row mod matrix (source -> destination, depth), LFO RATE
 *          and SHAPE
 *   CPU    the per-stage CPU meter and ETCH DS's CPU TEST
 *
 * The tab row is the top row: LEFT / RIGHT there change tab (ETCH: the
 * channel tabs; ETCH's B + LEFT / RIGHT changes screens on the Playdate).
 * Every value: LEFT / RIGHT or A + LEFT / RIGHT by one, A + UP / DOWN coarse,
 * B + A default, A held + crank rides.  A on a channel row plays it.
 */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "gfx1.h"
#include "app_glue.h"
#include "audio.h"
#include "corrosion.h"
#include "crank.h"
#include "cpl.h"
#include "pump.h"
#include "plat.h"
#include "layers.h"

enum { T_BASS, T_RUST, T_KICK, T_SNARE, T_HAT, T_OPEN, T_METAL, T_NOISE, T_FX, T_MOD, T_EVO, T_CPU, T_COUNT };
static const char *const tab_names[T_COUNT] = { "BAS", "RST", "KCK", "SNR", "HAT", "OPN", "MTL", "NSE", "FX", "MOD", "EVO", "CPU" };
#define IS_DRUM(t) ((t) >= T_KICK && (t) <= T_NOISE)
static const uint8_t lane_mute_bit[6] = { 1, 2, 3, 4, 5, 6 };
static uint8_t tab = T_BASS, on_tabs;
static uint8_t lane;                     /* drum pages: 0..3 */

/* ---- BASS ------------------------------------------------------------------------- */
#define G(field) static int g_##field(void) { return base_acid.field; } \
                 static void s_##field(int v) { base_acid.field = (uint8_t)v; }
G(wave) G(envmod) G(decay) G(accent) G(slide) G(drive) G(drive_amt) G(keytrack) G(level)
static int g_tune(void) { return base_acid.tune; }
static void s_tune(int v) { base_acid.tune = (int8_t)v; }
static int g_cut(void) { return crank_ui.stored_cut; }
static void s_cut(int v) { crank_set_stored((uint8_t)v, crank_ui.stored_res); base_acid.cutoff = (uint8_t)v; }
static int g_res(void) { return crank_ui.stored_res; }
static void s_res(int v) { crank_set_stored(crank_ui.stored_cut, (uint8_t)v); base_acid.res = (uint8_t)v; }
static int g_gate(void) { return SH->proj.synth.gate; }
static void s_gate(int v) { SH->proj.synth.gate = (uint8_t)v; app_send(CMD(CMD_TEMPO, 0)); }
static void f_wave(int v, char *o) { static const char *const n[4] = { "SAW", "SQUARE", "HOLLOW", "BITE" }; strcpy(o, n[v & 3]); }
static void f_drive(int v, char *o) { static const char *const n[4] = { "OFF", "SOFT", "HARD", "FOLD" }; strcpy(o, n[v & 3]); }
static void f_keytrack(int v, char *o) { strcpy(o, v ? "KEY" : "303"); }
static void f_tune(int v, char *o) { sprintf(o, "%+d", v); }
static void f_gate(int v, char *o) { static const char *const n[4] = { "SHORT", "MID", "LONG", "FULL" }; strcpy(o, n[v & 3]); }
static void a_hear(void) { live_note(EV_NOTE, 24, 1); live_note(EV_GATE_OFF, 0, 0); }
static void a_lane(void)
{
    if (cpl_motion_playing()) { cpl_lane_clear(SH->cur); ui_message("CRANK LANE CLEARED"); }
    else ui_message("B+UP RECORDS A CRANK LANE (FROM THE NEXT BAR)");
}
static const row_t bass_rows[] = {
    { "WAVE", g_wave, s_wave, 0, 3, 1, 0, f_wave, a_hear },
    { "CUTOFF", g_cut, s_cut, 0, 127, 8, 30, NULL, a_hear },
    { "RESO", g_res, s_res, 0, 127, 8, 92, NULL, a_hear },
    { "ENV MOD", g_envmod, s_envmod, 0, 127, 8, 80, NULL, a_hear },
    { "DECAY", g_decay, s_decay, 0, 127, 8, 56, NULL, a_hear },
    { "ACCENT", g_accent, s_accent, 0, 127, 8, 80, NULL, a_hear },
    { "SLIDE", g_slide, s_slide, 0, 127, 8, 40, NULL, a_hear },
    { "DRIVE", g_drive, s_drive, 0, 3, 1, DRV_SOFT, f_drive, a_hear },
    { "DRV AMT", g_drive_amt, s_drive_amt, 0, 127, 8, 48, NULL, a_hear },
    { "FILTER", g_keytrack, s_keytrack, 0, 1, 1, 0, f_keytrack, a_hear },
    { "TUNE", g_tune, s_tune, -12, 12, 12, 0, f_tune, a_hear },
    { "LEVEL", g_level, s_level, 0, 127, 8, 100, NULL, a_hear },
    { "GATE", g_gate, s_gate, 0, 3, 1, 1, f_gate, a_hear },
    { "CRANK LANE", NULL, NULL, 0, 0, 0, 0, NULL, a_lane },
};

/* ---- the drum pages (ETCH DS's DRUM page) ------------------------------------------- */
enum { D_KIT, D_MODEL, D_PITCH, D_END, D_SWEEP, D_DECAY, D_NOISE, D_DRIVE, D_TUNE, D_LEVEL };
static uint16_t hz_tab[128];
static uint8_t dr_pending;
static uint32_t dr_last_ms;

static void dr_tables(void)
{
    uint32_t i, hz = 20u << 16;                        /* 20 Hz .. ~12.4 kHz, exponential (ETCH DS) */
    for (i = 0; i < 128; i++) { hz_tab[i] = (uint16_t)(hz >> 16); hz = (uint32_t)(((uint64_t)hz * 68916u) >> 16); }
}
static uint8_t dr_kit(void) { return lane < NUM_LANES ? (uint8_t)(SH->proj.pat[SH->cur].kit[lane] & 3) : 0; }
static drum_def_t *dr_def(void) { return &foundry_defs[lane][dr_kit()]; }
static int isqrt_i(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
static int dr_hz_v(int hz) { int i; for (i = 0; i < 127 && hz_tab[i + 1] <= hz; i++) { } return i; }

static int dr_get(int k)
{
    const drum_def_t *d = dr_def();
    switch (k) {
    case D_KIT: return dr_kit();
    case D_MODEL: return d->type;
    case D_PITCH: return dr_hz_v(d->f0);
    case D_END: return dr_hz_v(d->f1);
    case D_SWEEP: return isqrt_i(((int)d->pitch_ms - 1) * 80);
    case D_DECAY: return isqrt_i(((int)d->amp_ms - 10) * 8);
    case D_NOISE: return d->noise >> 1;
    case D_DRIVE: return d->drive >> 1;
    case D_TUNE: return foundry_tune[lane];
    default: return foundry_level[lane];
    }
}

static void dr_set(int k, int v)
{
    drum_def_t *d = dr_def();
    if (v == dr_get(k)) return;
    switch (k) {
    case D_KIT: if (lane < NUM_LANES) app_set_kit(lane, (uint8_t)v); return;
    case D_TUNE: foundry_tune[lane] = (uint8_t)v; if (lane < NUM_LANES) SH->proj.drum_tune[lane] = (uint8_t)v; return;
    case D_LEVEL: foundry_level[lane] = (uint8_t)v; if (lane < NUM_LANES) SH->proj.drum_level[lane] = (uint8_t)v; return;
    case D_MODEL: d->type = (uint8_t)v; break;
    case D_PITCH: d->f0 = hz_tab[v]; break;
    case D_END: d->f1 = hz_tab[v]; break;
    case D_SWEEP: d->pitch_ms = (uint16_t)(1 + v * v / 80); break;
    case D_DECAY: d->amp_ms = (uint16_t)(10 + v * v / 8); foundry_set_len(d); break;
    case D_NOISE: d->noise = (uint8_t)(v * 2); break;
    default: d->drive = (uint8_t)(v * 2); break;
    }
    dr_pending = 1;                                    /* re-synthesise (rate-limited) */
}

#define DG(k) static int gd_##k(void) { return dr_get(k); } static void sd_##k(int v) { dr_set(k, v); }
DG(D_KIT) DG(D_MODEL) DG(D_PITCH) DG(D_END) DG(D_SWEEP) DG(D_DECAY) DG(D_NOISE) DG(D_DRIVE) DG(D_TUNE) DG(D_LEVEL)
static void fd_kit(int v, char *o) { if (lane < NUM_LANES) strcpy(o, foundry_kit_names[lane][v & 3]); else strcpy(o, lane == 4 ? "METAL" : "NOISE"); }
static void fd_model(int v, char *o) { strcpy(o, foundry_model_names[v % DT_COUNT]); }
static void fd_pitch(int v, char *o) { sprintf(o, "%d HZ", hz_tab[v & 127]); }
static void fd_sweep(int v, char *o) { sprintf(o, "%d MS", 1 + v * v / 80); }
static void fd_decay(int v, char *o) { sprintf(o, "%d MS", 10 + v * v / 8); }
static void fd_tune(int v, char *o) { sprintf(o, "%+d", v - 8); }
static int g_mute(void) { return (SH->proj.mutes >> lane_mute_bit[lane]) & 1; }
static void s_mute(int v) { if (v != g_mute()) app_toggle_mute(lane_mute_bit[lane]); }
static int g_lock(void)
{
    if (lane >= NUM_LANES) return LD->pat[SH->cur & 31].lock;
    return (SH->proj.pat[SH->cur].lock >> (lane + 1)) & 1;
}
static void s_lock(int v)
{
    pattern_t p;
    if (lane >= NUM_LANES) { LD->pat[SH->cur & 31].lock = (uint8_t)v; return; }
    memcpy(&p, &SH->proj.pat[SH->cur], sizeof p);
    p.lock = (uint8_t)((p.lock & ~(PLOCK_LANE0 << lane)) | (v ? (PLOCK_LANE0 << lane) : 0));
    app_replace(&p, 0);
}
static void f_onoff(int v, char *o) { strcpy(o, v ? "ON" : "OFF"); }
static void a_drum(void)
{
    if (lane < NUM_LANES) app_audition(lane, dr_kit());
    else live_note(EV_DRUMS, (uint8_t)(1u << lane), 0);
}
static const row_t drum_rows[] = {
    { "KIT", gd_D_KIT, sd_D_KIT, 0, 3, 1, 0, fd_kit, a_drum },
    { "MODEL", gd_D_MODEL, sd_D_MODEL, 0, DT_COUNT - 1, 1, 0, fd_model, a_drum },
    { "PITCH", gd_D_PITCH, sd_D_PITCH, 0, 127, 8, 40, fd_pitch, a_drum },
    { "PITCH END", gd_D_END, sd_D_END, 0, 127, 8, 20, fd_pitch, a_drum },
    { "SWEEP", gd_D_SWEEP, sd_D_SWEEP, 0, 127, 8, 30, fd_sweep, a_drum },
    { "DECAY", gd_D_DECAY, sd_D_DECAY, 0, 127, 8, 40, fd_decay, a_drum },
    { "NOISE", gd_D_NOISE, sd_D_NOISE, 0, 127, 8, 0, NULL, a_drum },
    { "DRIVE", gd_D_DRIVE, sd_D_DRIVE, 0, 127, 8, 0, NULL, a_drum },
    { "TUNE", gd_D_TUNE, sd_D_TUNE, 0, 15, 4, 8, fd_tune, a_drum },
    { "LEVEL", gd_D_LEVEL, sd_D_LEVEL, 0, 15, 4, 15, NULL, a_drum },
    { "MUTE", g_mute, s_mute, 0, 1, 1, 0, f_onoff, NULL },
    { "LOCK", g_lock, s_lock, 0, 1, 1, 0, f_onoff, NULL },
};

/* ---- RUST (an ETCH DS layer) --------------------------------------------------------- */
#define GR(f) static int gr_##f(void) { return LD->rust.f; } static void sr_##f(int v) { LD->rust.f = (uint8_t)v; }
GR(wave_a) GR(wave_b) GR(interval) GR(detune) GR(sync) GR(ring) GR(cutoff) GR(res) GR(envmod) GR(decay) GR(mode) GR(gate) GR(level)
static void f_rmode(int v, char *o) { strcpy(o, v ? "PARA 3" : "MONO"); }
static void f_semis(int v, char *o) { sprintf(o, "+%d", v); }
#define GL(f) static int gl_##f(void) { return LD->pat[SH->cur & 31].f; } \
              static void sl_##f(int v) { LD->pat[SH->cur & 31].f = (uint8_t)v; LD->pat[SH->cur & 31].lock = 1; LD->manual = 1; }
GL(rust_len) GL(metal_len) GL(noise_len)
static int g_llock(void) { return LD->pat[SH->cur & 31].lock; }
static void s_llock(int v) { LD->pat[SH->cur & 31].lock = (uint8_t)v; }
static void a_rust(void) { uint8_t n[3] = { 36, 39, 43 }; rust_note(n, LD->rust.mode ? 3 : 1, 1, 0); rust_gate_off(); }
static const row_t rust_rows[] = {
    { "OSC A", gr_wave_a, sr_wave_a, 0, 3, 1, RW_SAW, f_wave, a_rust },
    { "OSC B", gr_wave_b, sr_wave_b, 0, 3, 1, RW_SQUARE, f_wave, a_rust },
    { "INTERVAL", gr_interval, sr_interval, 0, 24, 12, 7, f_semis, a_rust },
    { "DETUNE", gr_detune, sr_detune, 0, 127, 8, 20, NULL, a_rust },
    { "SYNC", gr_sync, sr_sync, 0, 1, 1, 0, f_onoff, a_rust },
    { "RING", gr_ring, sr_ring, 0, 127, 8, 0, NULL, a_rust },
    { "CUTOFF", gr_cutoff, sr_cutoff, 0, 127, 8, 52, NULL, a_rust },
    { "RESO", gr_res, sr_res, 0, 127, 8, 70, NULL, a_rust },
    { "ENV MOD", gr_envmod, sr_envmod, 0, 127, 8, 60, NULL, a_rust },
    { "DECAY", gr_decay, sr_decay, 0, 127, 8, 50, NULL, a_rust },
    { "MODE", gr_mode, sr_mode, 0, 1, 1, RM_MONO, f_rmode, a_rust },
    { "GATE", gr_gate, sr_gate, 1, 15, 4, 9, NULL, a_rust },
    { "LEVEL", gr_level, sr_level, 0, 127, 8, 70, NULL, a_rust },
    { "RUST LENGTH", gl_rust_len, sl_rust_len, 1, 16, 4, 16, NULL, NULL },
    { "METAL LENGTH", gl_metal_len, sl_metal_len, 1, 16, 4, 16, NULL, NULL },
    { "NOISE LENGTH", gl_noise_len, sl_noise_len, 1, 16, 4, 16, NULL, NULL },
    { "LAYER LOCK", g_llock, s_llock, 0, 1, 1, 0, f_onoff, NULL },
};

/* ---- EVO: sound evolution and its knob locks (an ETCH DS layer) ---------------------- */
static int g_evo(void) { return LD->evo_on; }
static void s_evo(int v) { LD->evo_on = (uint8_t)v; if (v) layers_evo_anchor(); }
#define GK(k) static int gk##k(void) { return (LD->evo_lock >> k) & 1; } \
              static void sk##k(int v) { LD->evo_lock = (uint8_t)((LD->evo_lock & ~(1u << k)) | (v ? 1u << k : 0)); }
GK(0) GK(1) GK(2) GK(3) GK(4) GK(5) GK(6) GK(7)
static void f_lock(int v, char *o) { strcpy(o, v ? "LOCKED" : "FREE"); }
static const row_t evo_rows[] = {
    { "SOUND EVO", g_evo, s_evo, 0, 1, 1, 0, f_onoff, NULL },
    { "ENV MOD", gk0, sk0, 0, 1, 1, 0, f_lock, NULL },
    { "DECAY", gk1, sk1, 0, 1, 1, 0, f_lock, NULL },
    { "ACCENT", gk2, sk2, 0, 1, 1, 0, f_lock, NULL },
    { "DRV AMT", gk3, sk3, 0, 1, 1, 0, f_lock, NULL },
    { "RUST RES", gk4, sk4, 0, 1, 1, 0, f_lock, NULL },
    { "RUST CUT", gk5, sk5, 0, 1, 1, 0, f_lock, NULL },
    { "DLY MIX", gk6, sk6, 0, 1, 1, 0, f_lock, NULL },
    { "COMB MIX", gk7, sk7, 0, 1, 1, 0, f_lock, NULL },
};

/* ---- FX ------------------------------------------------------------------------------ */
#define GF(field) static int gf_##field(void) { return base_fx.field; } \
                  static void sf_##field(int v) { base_fx.field = (uint8_t)v; }
GF(drive) GF(crush_bits) GF(crush_hold) GF(dly_steps) GF(dly_fb) GF(dly_mix) GF(dly_rot) GF(comb_note) GF(comb_fb) GF(comb_mix) GF(limit)
static void f_bits(int v, char *o) { if (v >= 16) strcpy(o, "OFF"); else sprintf(o, "%d BIT", v); }
static void f_hold(int v, char *o) { if (v <= 1) strcpy(o, "OFF"); else sprintf(o, "%d", v); }
static void f_steps(int v, char *o) { sprintf(o, "%d/16", v); }
static void f_note(int v, char *o) { strcpy(o, note_name((uint8_t)v)); }
static const row_t fx_rows[] = {
    { "DRIVE", gf_drive, sf_drive, 0, 127, 8, 0, NULL, NULL },
    { "CRUSH", gf_crush_bits, sf_crush_bits, 4, 16, 2, 16, f_bits, NULL },
    { "HOLD", gf_crush_hold, sf_crush_hold, 1, 16, 2, 1, f_hold, NULL },
    { "DELAY", gf_dly_steps, sf_dly_steps, 1, 8, 1, 3, f_steps, NULL },
    { "FEEDBACK", gf_dly_fb, sf_dly_fb, 0, 127, 8, 50, NULL, NULL },
    { "DLY MIX", gf_dly_mix, sf_dly_mix, 0, 127, 8, 0, NULL, NULL },
    { "ROT", gf_dly_rot, sf_dly_rot, 0, 127, 8, 40, NULL, NULL },
    { "COMB NOTE", gf_comb_note, sf_comb_note, 0, 60, 12, 24, f_note, NULL },
    { "COMB FB", gf_comb_fb, sf_comb_fb, 0, 127, 8, 60, NULL, NULL },
    { "COMB MIX", gf_comb_mix, sf_comb_mix, 0, 127, 8, 0, NULL, NULL },
    { "LIMIT", gf_limit, sf_limit, 0, 1, 1, 1, f_onoff, NULL },
};

/* ---- MOD ----------------------------------------------------------------------------- */
#define GM(i) \
    static int gms##i(void) { return cplp.mod[i].src; } static void sms##i(int v) { cplp.mod[i].src = (uint8_t)v; } \
    static int gmd##i(void) { return cplp.mod[i].dst; } static void smd##i(int v) { cplp.mod[i].dst = (uint8_t)v; } \
    static int gmx##i(void) { return cplp.mod[i].depth; } static void smx##i(int v) { cplp.mod[i].depth = (int8_t)v; }
GM(0) GM(1) GM(2) GM(3)
static void f_src(int v, char *o) { strcpy(o, mod_src_names[v % MS_COUNT]); }
static void f_dst(int v, char *o) { strcpy(o, mod_dst_names[v % MD_COUNT]); }
static void f_depth(int v, char *o) { sprintf(o, "%+d", v); }
static int g_lrate(void) { return cplp.lfo_rate; }
static void s_lrate(int v) { cplp.lfo_rate = (uint8_t)v; }
static int g_lshape(void) { return cplp.lfo_shape; }
static void s_lshape(int v) { cplp.lfo_shape = (uint8_t)v; }
static void f_lshape(int v, char *o) { static const char *const n[4] = { "TRI", "SQUARE", "SAW", "S+H" }; strcpy(o, n[v & 3]); }
#define MODROWS(i, name) \
    { name, gms##i, sms##i, 0, MS_COUNT - 1, 1, 0, f_src, NULL }, \
    { "  TO", gmd##i, smd##i, 0, MD_COUNT - 1, 1, 0, f_dst, NULL }, \
    { "  DEPTH", gmx##i, smx##i, -127, 127, 16, 0, f_depth, NULL },
static const row_t mod_rows[] = {
    MODROWS(0, "MOD 1 FROM") MODROWS(1, "MOD 2 FROM") MODROWS(2, "MOD 3 FROM") MODROWS(3, "MOD 4 FROM")
    { "LFO RATE", g_lrate, s_lrate, 0, 15, 4, 6, NULL, NULL },
    { "LFO SHAPE", g_lshape, s_lshape, 0, 3, 1, 0, f_lshape, NULL },
};

/* ---- CPU ------------------------------------------------------------------------------ */
static fx_params_t fx_saved;
static int g_stress(void) { return SH->stress; }
static void s_stress(int v)
{
    if (v) {
        fx_saved = base_fx;
        base_fx.drive = 50; base_fx.crush_bits = 10; base_fx.crush_hold = 3;
        base_fx.dly_mix = 60; base_fx.dly_fb = 80; base_fx.comb_mix = 40; base_fx.comb_fb = 90;
    } else base_fx = fx_saved;
    app_send(CMD(CMD_STRESS, (uint32_t)v));
}
static const row_t cpu_rows[] = {
    { "CPU TEST", g_stress, s_stress, 0, 1, 1, 0, f_onoff, NULL },
};

#define DRUMLIST { drum_rows, sizeof drum_rows / sizeof drum_rows[0], 0, 0, 0 }
static list_t lists[T_COUNT] = {
    { bass_rows, sizeof bass_rows / sizeof bass_rows[0], 0, 0, 0 },
    { rust_rows, sizeof rust_rows / sizeof rust_rows[0], 0, 0, 0 },
    DRUMLIST, DRUMLIST, DRUMLIST, DRUMLIST, DRUMLIST, DRUMLIST,
    { fx_rows, sizeof fx_rows / sizeof fx_rows[0], 0, 0, 0 },
    { mod_rows, sizeof mod_rows / sizeof mod_rows[0], 0, 0, 0 },
    { evo_rows, sizeof evo_rows / sizeof evo_rows[0], 0, 0, 0 },
    { cpu_rows, 1, 0, 0, 0 },
};

/* ---- the screen ------------------------------------------------------------------------- */
static void synth_event(const ui_ev_t *e)
{
    list_t *l = &lists[tab];
    if (on_tabs) {
        if (e->nav_x || e->adj_x) {
            tab = (uint8_t)((tab + e->nav_x + e->adj_x + T_COUNT) % T_COUNT);
            lane = (uint8_t)(IS_DRUM(tab) ? tab - T_KICK : lane);
        }
        if (e->nav_y > 0) on_tabs = 0;
        if (e->a_tap) { if (tab == T_BASS) a_hear(); else if (tab == T_RUST) a_rust(); else if (IS_DRUM(tab)) a_drum(); }
        return;
    }
    if (e->nav_y < 0 && l->sel == 0) { on_tabs = 1; return; }
    list_event(l, e);
}

static uint8_t synth_can_ride(void) { return !on_tabs && list_can_ride(&lists[tab]); }

static void draw_cpu(void)
{
    double hz = plat_tick_hz(), tb = hz * AUDIO_BLOCK / 32728.498;
    int y = 108, i;
    static const int st[] = { ST_ACID, ST_DRUMS, ST_DRIVE, ST_CRUSH, ST_DELAY, ST_COMB, ST_LIMIT, ST_EVENTS };
    static const char *const nm[] = { "ACID", "DRUMS", "DRIVE", "CRUSH", "DELAY", "COMB", "LIMIT", "EVENTS" };
#define PCT(v) ((double)(v) / 100.0 * 131072.0 / tb)
    gfx_textf(8, y - 14, F_MAIN, 1, C_BLACK, "PERCENT OF REAL TIME   CLOCK %.1f MHZ", hz / 1e6);
    for (i = 0; i < 8; i++) {
        double v = PCT(astats.avg_pct[st[i]]);
        int x = 8 + (i % 2) * 196, yy = y + (i / 2) * 11;
        gfx_textf(x, yy, F_MAIN, 1, C_BLACK, "%-7s %5.1f", nm[i], v);
        gfx_fill(x + 90, yy + 1, (int)(v * 4) > 96 ? 96 : (int)(v * 4), 5, C_BLACK);
    }
    y += 48;
    {
        double cb = pstats.frames_last ? 100.0 * (double)pstats.cb_sum_last / (pstats.frames_last / 44100.0 * hz) : 0;
        gfx_textf(8, y, F_MAIN, 1, C_BLACK, "SEQ %.1f  RESAMPLE %.1f  ENGINE %.1f  WORST BLOCK %.1f",
                  PCT(pstats.seq_pct), PCT(pstats.rs_pct), PCT(astats.avg_total), PCT(astats.max_total_pct));
        gfx_textf(8, y + 11, F_MAIN, 1, C_BLACK, "CALLBACK %.1f%%  WORST %.2f MS  DROPS %lu  LATE %lu",
                  cb, 1000.0 * (double)pstats.cb_max_last / hz, (unsigned long)pump_cmd_drops, (unsigned long)SH->gates_late);
        gfx_textf(8, y + 22, F_MAIN, 1, C_BLACK, "UI FRAME %.1f MS", plat_ui_ms());
    }
#undef PCT
}

static void synth_draw(void)
{
    int i;
    for (i = 0; i < T_COUNT; i++) {
        int x = 2 + i * 33, sel = i == tab;
        if (sel && on_tabs) gfx_fill(x - 1, 66, 32, 11, C_BLACK);
        gfx_text_c(x + 15, 68, F_MAIN, 1, sel && on_tabs ? C_WHITE : C_BLACK, tab_names[i]);
        if (sel) gfx_fill(x, 77, 30, 2, C_BLACK);
    }
    gfx_hline(0, 80, GFX_W, C_D50);
    if (tab == T_CPU) {
        list_draw(&lists[tab], 8, 86, 384, 12, 1);
        draw_cpu();
        return;
    }
    list_draw(&lists[tab], 8, 86, tab == T_BASS ? 240 : 384, 12, 11);
    if (tab == T_BASS) {                               /* the scope of ACID's filter */
        gfx_text(264, 86, F_SMALL, 1, C_BLACK, "LIVE CUTOFF");
        gfx_textf(264, 94, F_MAIN, 2, C_BLACK, "%d", (int)((crank_cut_now_q8 + 128) >> 8));
        gfx_text(264, 114, F_SMALL, 1, C_BLACK, "LIVE RESO");
        gfx_textf(264, 122, F_MAIN, 2, C_BLACK, "%d", (int)((crank_res_now_q8 + 128) >> 8));
        gfx_text(264, 142, F_SMALL, 1, C_BLACK, "CRANK LANE");
        gfx_text(264, 150, F_MAIN, 1, C_BLACK, cpl_motion_playing() ? "PLAYING" : "NONE");
    }
}

/* the drum page re-synthesises at most every 200 ms while values change */
void synth_frame(void)
{
    if (dr_pending && ui_now_ms - dr_last_ms >= 200) {
        foundry_guard_rebuild(lane, dr_kit());
        if (lane >= NUM_LANES) LD->drums45[lane - NUM_LANES] = foundry_defs[lane][0];   /* saved in the sidecar */
        dr_pending = 0;
        dr_last_ms = ui_now_ms;
    }
}

static const char *synth_hint(void)
{
    if (on_tabs) return "L/R CHANNEL   A HEAR   DOWN EDIT";
    if (tab == T_MOD) return "L/R OR A+D-PAD CHANGE   B+A RESET   A+CRANK RIDE";
    return "L/R CHANGE   A+U/D COARSE   B+A DEFAULT   A HEAR   A+CRANK RIDE";
}

static void synth_enter(void) { if (!hz_tab[0]) dr_tables(); }

const screen_t scr_synth = { "SYNTH", synth_event, synth_draw, synth_can_ride, synth_hint, synth_enter };
