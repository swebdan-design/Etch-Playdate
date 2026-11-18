/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * LIVE: home.  The pattern (BASS and ETCH's four drum lanes), the pads, the
 * macros.  ETCH's LIVE, with the grammar of every screen:
 *
 *   grid      A on an empty bass step writes the last note; A on a note
 *             removes it; A + LEFT / RIGHT a semitone, A + UP / DOWN an
 *             octave; B + A removes.  Drum steps: A toggles, B + A clears.
 *             Lane name: A mutes; A + LEFT / RIGHT puts the lane in mute
 *             group 1 or 2 (or none).
 *   pads      FILL (one shot) . REGEN . EVOLVE . UNDO . scenes S1-S4 (A on an
 *             empty scene stores the sound, A recalls, B + A clears, A held +
 *             crank morphs towards it) . mute groups M1 M2 (A toggles)
 *   macros    LEFT / RIGHT choose; A + D-pad change; A held + crank rides.
 *             PAT queues a pattern for the next bar.
 *   B tap     FILL on / off (latched), as on ETCH
 *
 * Steps CORROSION has just changed start solid and fade through 75 / 50 /
 * 25 % dither over two bars.
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

#define GRID_Y   68
#define ROW_H    15
#define CELL_X   46
#define CELL_W   21
#define PAD_Y    146
#define MAC_Y    170
#define INFO_Y   206

enum { R_BASS, R_KICK, R_SNARE, R_HAT, R_OPEN, R_PADS, R_MACROS };
static const char *const lane_names[5] = { "BASS", "KICK", "SNARE", "HAT", "OPEN" };
static const char *const pad_names[10] = { "FILL", "REGEN", "EVOLVE", "UNDO", "S1", "S2", "S3", "S4", "M1", "M2" };
#define N_MACROS 8
static const char *const mac_names[N_MACROS] = { "PAT", "ENV", "DEC", "ACC", "DRV", "RGN", "EVO", "MUT" };

static int8_t row = R_BASS, col = 0, pad = 0, mac = 0;
static uint8_t last_note = 24, rgn_what = GEN_ALL;
static uint32_t ride_ms;

/* ---- the glow of fresh CORROSION changes ---------------------------------------- */
static pattern_t seen;
static uint8_t seen_pat = 0xFF, seen_commits;
static uint32_t glow_t0[5][NUM_STEPS];

static void track_glow(void)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    int s, r;
    if (SH->cur != seen_pat) {
        memcpy(&seen, p, sizeof seen);
        seen_pat = SH->cur;
        seen_commits = SH->evo_commits;
        memset(glow_t0, 0, sizeof glow_t0);
        return;
    }
    if (!memcmp(&seen, p, sizeof seen)) return;
    if (SH->evo_commits != seen_commits) {
        for (s = 0; s < NUM_STEPS; s++) {
            if (seen.note[s] != p->note[s] || seen.flags[s] != p->flags[s]) glow_t0[0][s] = ui_now_ms | 1;
            for (r = 1; r < 5; r++)
                if (((seen.drum[r - 1] ^ p->drum[r - 1]) >> s) & 1) glow_t0[r][s] = ui_now_ms | 1;
        }
    }
    memcpy(&seen, p, sizeof seen);
    seen_commits = SH->evo_commits;
}

static int glow_colour(int r, int s)
{
    uint32_t two_bars = 8u * 60000u / (SH->proj.synth.bpm ? SH->proj.synth.bpm : 120), t;
    if (!glow_t0[r][s]) return -1;
    t = ui_now_ms - glow_t0[r][s];
    if (t >= two_bars) { glow_t0[r][s] = 0; return -1; }
    switch (t * 4 / two_bars) {
    case 0: return C_BLACK;
    case 1: return C_D75;
    case 2: return C_D50;
    default: return C_D25;
    }
}

/* ---- the macros ------------------------------------------------------------------------ */
static int mac_get(int m)
{
    switch (m) {
    case 0: return SH->queued != 0xFF ? SH->queued : SH->cur;
    case 1: return base_acid.envmod;
    case 2: return base_acid.decay;
    case 3: return base_acid.accent;
    case 4: return base_acid.drive;
    case 5: return rgn_what == GEN_ALL ? 0 : rgn_what == GEN_BASS ? 1 : 2;
    case 6: return SH->proj.gen.mode;
    default: return SH->proj.gen.mutation;
    }
}

static void mac_set(int m, int v)
{
    static const int hi[N_MACROS] = { 31, 127, 127, 127, 3, 2, 2, 15 };
    if (v < 0) v = m == 0 ? 31 : 0;
    if (v > hi[m]) v = m == 0 ? 0 : hi[m];
    switch (m) {
    case 0: app_queue((uint8_t)v); break;
    case 1: base_acid.envmod = (uint8_t)v; break;
    case 2: base_acid.decay = (uint8_t)v; break;
    case 3: base_acid.accent = (uint8_t)v; break;
    case 4: base_acid.drive = (uint8_t)v; break;
    case 5: rgn_what = v == 0 ? GEN_ALL : v == 1 ? GEN_BASS : GEN_DRUMS; break;
    case 6: SH->proj.gen.mode = (uint8_t)v; break;
    default: SH->proj.gen.mutation = (uint8_t)v; break;
    }
}

static void mac_text(int m, char *b)
{
    static const char *const drv[4] = { "OFF", "SOFT", "HARD", "FOLD" };
    static const char *const rg[3] = { "ALL", "BASS", "DRUM" };
    int v = mac_get(m);
    switch (m) {
    case 0: snprintf(b, 16, "%02d", v + 1); break;
    case 4: snprintf(b, 16, "%s", drv[v & 3]); break;
    case 5: snprintf(b, 16, "%s", rg[v]); break;
    case 6: snprintf(b, 16, "%.5s", corr_mode_names[v]); break;
    default: snprintf(b, 16, "%d", v); break;
    }
}

/* ---- events -------------------------------------------------------------------------- */
static void bass_edit(const ui_ev_t *e)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    uint8_t f = p->flags[col], n = p->note[col], t = f & STEP_TYPE_MASK;
    if (e->del) { app_set_bass((uint8_t)col, n, (uint8_t)(f & ~STEP_TYPE_MASK & ~STEP_SLIDE)); return; }
    if (e->a_tap) {
        if (t == STEP_NOTE) app_set_bass((uint8_t)col, n, (uint8_t)(f & ~STEP_TYPE_MASK & ~STEP_SLIDE));
        else app_set_bass((uint8_t)col, last_note, (uint8_t)((f & ~STEP_TYPE_MASK) | STEP_NOTE));
        return;
    }
    if (e->adj_x || e->adj_y) {
        int nn = (t == STEP_NOTE ? n : last_note) + e->adj_x - e->adj_y * 12;
        if (nn < NOTE_MIN) nn = NOTE_MIN;
        if (nn > NOTE_MAX) nn = NOTE_MAX;
        last_note = (uint8_t)nn;
        app_set_bass((uint8_t)col, (uint8_t)nn, (uint8_t)((f & ~STEP_TYPE_MASK) | STEP_NOTE));
        if (!SH->playing) live_note(EV_NOTE, (uint8_t)nn, 0), live_note(EV_GATE_OFF, 0, 0);
    }
}

static void pad_do(int i)
{
    switch (i) {
    case 0: app_fill(); ui_message("FILL"); break;
    case 1: if (app_regen(rgn_what)) ui_message("REGEN: HALF THE BAR IS NEW"); break;
    case 2: app_evolve(); ui_message("EVOLVE AT THE NEXT BAR"); break;
    case 3: app_undo(); ui_message("UNDO"); break;
    case 4: case 5: case 6: case 7:
        if (cplp.scene[i - 4].used) cpl_scene_recall(i - 4); else cpl_scene_store(i - 4);
        break;
    default: cpl_mgroup_toggle(i - 8); break;
    }
}

static void live_event(const ui_ev_t *e)
{
    if (e->nav_y) {
        row = (int8_t)(row + e->nav_y);
        if (row < 0) row = 0;
        if (row > R_MACROS) row = R_MACROS;
    }
    if (e->nav_x) {
        if (row <= R_OPEN) { col = (int8_t)(col + e->nav_x); if (col < -1) col = -1; if (col > 15) col = 15; }
        else if (row == R_PADS) { pad = (int8_t)((pad + e->nav_x + 10) % 10); }
        else mac = (int8_t)((mac + e->nav_x + N_MACROS) % N_MACROS);
    }
    if (row <= R_OPEN && col < 0) {                       /* lane names */
        uint8_t bit = (uint8_t)(1u << row);
        if (e->a_tap) app_toggle_mute((uint8_t)row);
        if (e->adj_x) cpl_mgroup_assign(bit, (uint8_t)((cpl_mgroup_of(bit) + 3 + e->adj_x) % 3));
        return;
    }
    if (row == R_BASS) { bass_edit(e); return; }
    if (row <= R_OPEN) {
        if (e->a_tap) app_toggle_drum((uint8_t)(row - 1), (uint8_t)col);
        if (e->del && ((SH->proj.pat[SH->cur].drum[row - 1] >> col) & 1)) app_toggle_drum((uint8_t)(row - 1), (uint8_t)col);
        return;
    }
    if (row == R_PADS) {
        if (e->a_tap) pad_do(pad);
        if (e->del && pad >= 4 && pad < 8) cpl_scene_clear(pad - 4);
        if (e->ride_q8 && pad >= 4 && pad < 8) {
            if (ui_now_ms - ride_ms > 400) cpl_morph_begin(pad - 4);
            ride_ms = ui_now_ms;
            cpl_morph_by(e->ride_q8);
        }
        return;
    }
    /* macros */
    {
        int v = mac_get(mac);
        static int32_t acc;
        if (e->adj_x || e->adj_y) mac_set(mac, v + e->adj_x - e->adj_y * (mac == 0 ? 1 : mac >= 4 && mac <= 6 ? 1 : 8));
        if (e->ride_q8) {
            int step, per = (mac >= 4 && mac <= 6) || mac == 0 ? 30 * 256 : mac == 7 ? 12 * 256 : 3 * 256;
            acc += e->ride_q8;
            step = acc / per;
            acc -= step * per;
            if (step) mac_set(mac, v + step);
        }
        if (e->del) {
            static const int dflt[N_MACROS] = { 0, 80, 56, 80, DRV_SOFT, 0, EVO_DRIFT, 5 };
            if (mac) mac_set(mac, dflt[mac]);
        }
    }
}

static uint8_t live_can_ride(void)
{
    if (row == R_MACROS) return 1;
    if (row == R_PADS && pad >= 4 && pad < 8) return cplp.scene[pad - 4].used;
    return 0;
}

/* ---- drawing ----------------------------------------------------------------------- */
static void draw_cell(const pattern_t *p, int r, int s)
{
    int x = CELL_X + s * CELL_W, y = GRID_Y + r * ROW_H, g = glow_colour(r, s);
    int on_cursor = row == r && col == s, tc = C_BLACK;
    if ((s & 3) == 0) gfx_fill(x, y, CELL_W - 1, ROW_H - 1, C_D25);
    if (g >= 0) { gfx_fill(x, y, CELL_W - 1, ROW_H - 1, g); tc = g == C_BLACK || g == C_D75 ? C_WHITE : C_BLACK; }
    if (r == R_BASS) {
        uint8_t t = p->flags[s] & STEP_TYPE_MASK;
        if (t == STEP_NOTE) {
            char nb[4];
            snprintf(nb, sizeof nb, "%s", note_name(p->note[s]));
            if (p->flags[s] & STEP_ACCENT) {
                gfx_fill(x, y, CELL_W - 1, ROW_H - 1, C_BLACK);
                gfx_text(x + 2, y + 4, F_MAIN, 1, C_WHITE, nb);
            } else gfx_text(x + 2, y + 4, F_MAIN, 1, tc, nb);
        } else if (t == STEP_TIE) {
            gfx_fill(x, y + 6, CELL_W - 1, 2, tc);
        } else {
            gfx_fill(x + 9, y + 6, 2, 2, tc);
        }
        if (p->flags[s] & STEP_SLIDE) gfx_line(x + 12, y + 12, x + CELL_W, y + 12, C_BLACK);
        if (p->flags[s] & STEP_LOCK) gfx_fill(x, y, 3, 3, C_BLACK);
    } else if ((p->drum[r - 1] >> s) & 1) {
        gfx_fill(x + 4, y + 2, CELL_W - 9, ROW_H - 5, r == R_KICK ? C_BLACK : (tc == C_WHITE ? C_WHITE : C_BLACK));
        if (r != R_KICK) gfx_fill(x + 6, y + 4, CELL_W - 13, ROW_H - 9, tc == C_WHITE ? C_BLACK : C_WHITE);
    } else {
        gfx_fill(x + 9, y + 6, 2, 2, tc);
    }
    if (on_cursor) gfx_frame(x - 1, y - 1, CELL_W + 1, ROW_H + 1, C_XOR);
}

static void live_draw(void)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    int r, s, i;
    char b[64];
    track_glow();
    for (r = 0; r < 5; r++) {
        uint8_t muted = (SH->proj.mutes >> r) & 1, grp = cpl_mgroup_of((uint8_t)(1u << r));
        int sel = row == r && col < 0;
        if (sel) gfx_fill(1, GRID_Y + r * ROW_H - 1, 42, ROW_H, C_BLACK);
        gfx_text(4, GRID_Y + r * ROW_H + 4, F_MAIN, 1, sel ? C_WHITE : C_BLACK, lane_names[r]);
        if (muted) gfx_hline(3, GRID_Y + r * ROW_H + 7, 36, sel ? C_WHITE : C_BLACK);
        if (grp) gfx_textf(38, GRID_Y + r * ROW_H + 1, F_SMALL, 1, sel ? C_WHITE : C_BLACK, "%u", grp);
        for (s = 0; s < NUM_STEPS; s++) draw_cell(p, r, s);
    }
    /* the playhead */
    if (SH->playing) gfx_fill(CELL_X + SH->step * CELL_W, GRID_Y + 5 * ROW_H, CELL_W - 1, 3, C_BLACK);
    /* the lane's position */
    if (cpl_motion_playing()) gfx_hline(CELL_X + (crank_lane_pos >> 2) * CELL_W, GRID_Y + 5 * ROW_H + 4, CELL_W - 1, C_D50);

    /* pads */
    for (i = 0; i < 10; i++) {
        int x = 4 + i * 39 + (i >= 4 ? 6 : 0) + (i >= 8 ? 6 : 0), w = i < 4 ? 38 : 30, lit = 0, sel = row == R_PADS && pad == i;
        if (i >= 4) w = 30;
        if (i == 0) lit = ui_fill_on;
        if (i >= 4 && i < 8) lit = cplp.scene[i - 4].used;
        if (i >= 8) lit = cplp.mgroup[i - 8] && (SH->proj.mutes & cplp.mgroup[i - 8]) == cplp.mgroup[i - 8];
        if (i >= 4) x = 4 + 4 * 39 + 6 + (i - 4) * 33 + (i >= 8 ? 8 : 0);
        gfx_fill(x, PAD_Y, w, 17, lit ? C_BLACK : C_WHITE);
        gfx_frame(x, PAD_Y, w, 17, C_BLACK);
        gfx_text_c(x + w / 2, PAD_Y + 5, F_MAIN, 1, lit ? C_WHITE : C_BLACK, pad_names[i]);
        if (sel) gfx_frame(x - 2, PAD_Y - 2, w + 4, 21, C_BLACK);
    }
    if (cpl_morph_amount() && row == R_PADS && pad >= 4 && pad < 8)
        gfx_fill(4 + 4 * 39 + 6 + (pad - 4) * 33, PAD_Y + 18, (30 * cpl_morph_amount()) / 127, 2, C_BLACK);

    /* macros */
    for (i = 0; i < N_MACROS; i++) {
        int x = 4 + i * 49, sel = row == R_MACROS && mac == i;
        if (sel) gfx_fill(x - 2, MAC_Y - 2, 47, 30, C_BLACK);
        gfx_text(x, MAC_Y, F_SMALL, 1, sel ? C_WHITE : C_BLACK, mac_names[i]);
        mac_text(i, b);
        gfx_text(x, MAC_Y + 9, F_MAIN, 2, sel ? C_WHITE : C_BLACK, b);
    }

    /* what is under the cursor */
    if (row <= R_OPEN && col >= 0) {
        if (row == R_BASS) {
            uint8_t f = p->flags[col], t = f & STEP_TYPE_MASK;
            snprintf(b, sizeof b, "STEP %02d  %s%s%s%s  CHANCE %d/8  %s", col + 1,
                     t == STEP_NOTE ? note_name(p->note[col]) : t == STEP_TIE ? "TIE" : "REST",
                     f & STEP_ACCENT ? "  ACCENT" : "", f & STEP_SLIDE ? "  SLIDE" : "", f & STEP_LOCK ? "  LOCK" : "",
                     (p->prob[col] & 7) + 1, (p->prob[col] >> 4) ? "COND" : "");
        } else snprintf(b, sizeof b, "STEP %02d  %s  KIT %u", col + 1, lane_names[row], p->kit[row - 1] & 3);
        gfx_text(4, INFO_Y, F_MAIN, 1, C_BLACK, b);
    } else if (row <= R_OPEN) {
        gfx_textf(4, INFO_Y, F_MAIN, 1, C_BLACK, "%s %s  MUTE GROUP %s", lane_names[row],
                  (SH->proj.mutes >> row) & 1 ? "MUTED" : "ON",
                  cpl_mgroup_of((uint8_t)(1u << row)) == 1 ? "1" : cpl_mgroup_of((uint8_t)(1u << row)) == 2 ? "2" : "-");
    } else if (row == R_PADS && pad >= 4 && pad < 8) {
        gfx_textf(4, INFO_Y, F_MAIN, 1, C_BLACK, "SCENE %d  %s  MORPH %d%%", pad - 3,
                  cplp.scene[pad - 4].used ? "STORED" : "EMPTY", cpl_morph_amount() * 100 / 127);
    }
    if (cpl_motion_playing()) gfx_text_r(396, INFO_Y, F_MAIN, 1, C_BLACK, "CRANK LANE");
}

static const char *live_hint(void)
{
    if (row <= R_OPEN && col < 0) return "A MUTE   A+L/R MUTE GROUP   B FILL";
    if (row == R_BASS) return "A NOTE ON/OFF   A+L/R SEMI   A+U/D OCT   B+A DEL";
    if (row <= R_OPEN) return "A HIT ON/OFF   B+A CLEAR   B FILL";
    if (row == R_PADS) {
        if (pad >= 4 && pad < 8) return "A STORE/RECALL   A+CRANK MORPH   B+A CLEAR";
        return "A PLAY THE PAD   B FILL ON/OFF";
    }
    return "L/R CHOOSE   A+D-PAD CHANGE   A+CRANK RIDE";
}

static void live_enter(void) { }

const screen_t scr_live = { "LIVE", live_event, live_draw, live_can_ride, live_hint, live_enter };
