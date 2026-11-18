/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * The interface framework (see ui.h): the grammar, the screen row, the
 * status band, the hint line, the list widget.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "gfx1.h"
#include "app_glue.h"
#include "audio.h"
#include "corrosion.h"
#include "pump.h"
#include "crank.h"
#include "cpl.h"

uint8_t ui_fill_on;
uint32_t ui_now_ms;

static const screen_t *const screens[SCR_COUNT] = {
    &scr_synth, &scr_track, &scr_live, &scr_gen, &scr_song, &scr_file
};
static const char *const screen_names[SCR_COUNT] = { "SYNTH", "TRACK", "LIVE", "GEN", "SONG", "FILE" };
static uint8_t cur = SCR_LIVE;
static uint8_t body_dirty = 1, status_dirty = 1;

/* ---- messages --------------------------------------------------------------- */
static char msg[64];
static uint32_t msg_until;
void ui_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    msg_until = ui_now_ms + 2500;
}

void ui_redraw(void) { body_dirty = 1; }
uint8_t ui_screen(void) { return cur; }

void ui_goto(int s)
{
    cur = (uint8_t)((s + SCR_COUNT) % SCR_COUNT);
    if (screens[cur]->enter) screens[cur]->enter();
    body_dirty = status_dirty = 1;
}

/* ---- note names --------------------------------------------------------------- */
const char *note_name(uint8_t note)
{
    static const char *const nm[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    static char b[8];
    snprintf(b, sizeof b, "%s%u", nm[note % 12], (unsigned)(note / 12 + 1));
    return b;
}

void ui_bar(int x, int y, int w, int v, int lo, int hi, int selected)
{
    int f = hi > lo ? (v - lo) * (w - 2) / (hi - lo) : 0;
    gfx_hline(x, y + 2, w, selected ? C_WHITE : C_D50);
    gfx_fill(x, y, f + 2, 5, selected ? C_WHITE : C_BLACK);
}

/* ---- the list widget ------------------------------------------------------------ */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void list_event(list_t *l, const ui_ev_t *e)
{
    const row_t *r;
    if (!l->n) return;
    if (e->nav_y) l->sel = (uint8_t)clampi(l->sel + e->nav_y, 0, l->n - 1);
    r = &l->rows[l->sel];
    if (!r->get || !r->set) {
        if (e->a_tap && r->action) r->action();
        return;
    }
    {
        int v = r->get(), nv = v;
        nv += e->nav_x + e->adj_x;
        nv -= e->adj_y * (r->coarse ? r->coarse : 1);      /* UP = +coarse */
        if (e->del) nv = r->dflt;
        if (e->ride_q8) {                                   /* A held + crank */
            int span = r->max - r->min, step;
            /* the whole range in about 180 degrees, at least 6 degrees a step */
            int deg_per = span ? (180 * 256) / span : 256;
            if (deg_per < 6 * 256) deg_per = 6 * 256;
            l->ride_acc += e->ride_q8;
            step = l->ride_acc / deg_per;
            l->ride_acc -= step * deg_per;
            nv += step;
        }
        nv = clampi(nv, r->min, r->max);
        if (nv != v) { r->set(nv); ui_redraw(); }
        if (e->a_tap && r->action) r->action();
    }
}

uint8_t list_can_ride(const list_t *l)
{
    return l->n && l->rows[l->sel].get && l->rows[l->sel].set;
}

void list_draw(list_t *l, int x, int y, int w, int row_h, int visible)
{
    int i;
    if (l->sel < l->top) l->top = l->sel;
    if (l->sel >= l->top + visible) l->top = (uint8_t)(l->sel - visible + 1);
    for (i = 0; i < visible && l->top + i < l->n; i++) {
        const row_t *r = &l->rows[l->top + i];
        int ry = y + i * row_h, sel = (l->top + i) == l->sel;
        int tc = sel ? C_WHITE : C_BLACK;
        char b[24];
        if (sel) gfx_fill(x - 2, ry - 2, w + 4, row_h - 1, C_BLACK);
        gfx_text(x, ry, F_MAIN, 1, tc, r->caption);
        if (r->get) {
            int v = r->get();
            if (r->fmt) r->fmt(v, b); else snprintf(b, sizeof b, "%d", v);
            gfx_text_r(x + w, ry, F_MAIN, 1, tc, b);
            ui_bar(x + 96, ry + 1, w - 96 - 56, v, r->min, r->max, sel);
        } else if (r->action) {
            gfx_text_r(x + w, ry, F_MAIN, 1, tc, "A");
        }
    }
    if (l->top > 0) gfx_text(x + w - 4, y - 9, F_SMALL, 1, C_BLACK, "^");
    if (l->top + visible < l->n) gfx_text(x + w - 4, y + visible * row_h - 4, F_SMALL, 1, C_BLACK, "V");
}

/* ---- the grammar ---------------------------------------------------------------- */
static uint8_t a_used, b_used, a_with_b;
static uint8_t rep_dir, rep_cnt;
static uint32_t fill_entropy;

static uint8_t dir_events(const ui_in_t *in)
{
    /* the newest direction pressed, with key repeat while held */
    uint8_t d = in->pushed & BTN_DIRS;
    if (d) {
        rep_dir = d & (uint8_t)-(int8_t)d;       /* one direction */
        rep_cnt = 0;
        return rep_dir;
    }
    if (rep_dir && (in->held & rep_dir)) {
        if (++rep_cnt >= 16 && ((rep_cnt - 16) % 3) == 0) return rep_dir;
        return 0;
    }
    rep_dir = 0;
    return 0;
}

static void start_stop(void)
{
    if (!SH->playing) {
        uint16_t e = (uint16_t)(ui_entropy ^ hw_noise());
        evo_seed(e);                         /* the moment of the press seeds the living song */
        app_send(CMD(CMD_START, e));
    } else {
        app_send(CMD(CMD_STOP, 0));
    }
}

static void input(const ui_in_t *in)
{
    ui_ev_t e;
    uint8_t d, a = (in->held & BTN_A) != 0, b = (in->held & BTN_B) != 0;
    const screen_t *s = screens[cur];
    int32_t crank = in->crank_q8;

    memset(&e, 0, sizeof e);
    if (in->pushed) ui_entropy = (uint16_t)((ui_entropy * 31u) ^ hw_noise() ^ in->pushed);
    if (crank) ui_entropy = (uint16_t)(ui_entropy ^ (uint16_t)crank);
    (void)fill_entropy;

    if (in->pushed & BTN_A) { a_used = 0; a_with_b = b; }
    if (in->pushed & BTN_B) b_used = 0;
    d = dir_events(in);

    if (b) {                                   /* held B = SELECT */
        if (in->pushed & BTN_A) { e.del = 1; b_used = 1; a_used = 1; }
        if (d & BTN_LEFT) { ui_goto(cur - 1); b_used = 1; }
        if (d & BTN_RIGHT) { ui_goto(cur + 1); b_used = 1; }
        if (in->pushed & BTN_DOWN) { start_stop(); b_used = 1; }
        if (in->pushed & BTN_UP) { cpl_rec_arm(); b_used = 1; }
        if (crank || (in->docked && !crank_ui.docked)) b_used = 1;
    } else if (a) {                            /* A held + D-pad: change */
        if (d & BTN_LEFT) e.adj_x = -1;
        if (d & BTN_RIGHT) e.adj_x = 1;
        if (d & BTN_UP) e.adj_y = -1;
        if (d & BTN_DOWN) e.adj_y = 1;
        if (d) a_used = 1;
    } else {
        if (d & BTN_LEFT) e.nav_x = -1;
        if (d & BTN_RIGHT) e.nav_x = 1;
        if (d & BTN_UP) e.nav_y = -1;
        if (d & BTN_DOWN) e.nav_y = 1;
    }
    if ((in->released & BTN_A) && !a_used && !a_with_b && !b) e.a_tap = 1;
    if ((in->released & BTN_B) && !b_used && !a) e.b_tap = 1;
    if (in->released & BTN_B) b_used = 1;

    /* the crank: A held on a value rides it; otherwise the filter */
    if (a && !b && crank && s->can_ride && s->can_ride()) {
        e.ride_q8 = crank;
        a_used = 1;
        crank = 0;
    }
    if (a && crank) a_used = 1;                /* A + crank on the filter: no tap */
    crank_ui_frame(crank, in->docked, b, in->now_ms, SH->proj.synth.bpm);

    if (e.b_tap && cur == SCR_LIVE) {          /* ETCH LIVE: B = FILL on / off */
        ui_fill_on = (uint8_t)!ui_fill_on;
        app_fill_latch(ui_fill_on);
        status_dirty = 1;
        e.b_tap = 0;
    }
    if (e.nav_x || e.nav_y || e.adj_x || e.adj_y || e.a_tap || e.del || e.b_tap || e.ride_q8) {
        s->event(&e);
        body_dirty = 1;
    }
}

/* ---- the status band ------------------------------------------------------------- */
#define ARC_CX 31
#define ARC_CY 31
#define ARC_R  29
#define ARC_START 210.0f
#define ARC_SPAN  300.0f

static float arc_a(int32_t q8) { return ARC_START + ARC_SPAN * (float)q8 / (float)CRANK_MAX_Q8; }

static void draw_arc(void)
{
    int32_t cut = crank_cut_now_q8, res = crank_res_now_q8;
    float a = arc_a((int32_t)crank_ui.stored_cut << 8) * 3.14159265f / 180.0f;
    gfx_fill(0, 0, 64, 63, C_WHITE);
    gfx_arc(ARC_CX, ARC_CY, ARC_R, 1, ARC_START, ARC_START + ARC_SPAN, C_BLACK);
    if (cut > 0) gfx_arc(ARC_CX, ARC_CY, ARC_R, 5, ARC_START, arc_a(cut), C_BLACK);
    if (res > 0) gfx_arc(ARC_CX, ARC_CY, ARC_R - 8, 1, ARC_START, arc_a(res), C_BLACK);
    {   /* the stored value: a tick across the track */
        extern float sinf(float), cosf(float);
        float s = sinf(a), c = cosf(a);
        gfx_line((int)(ARC_CX + (ARC_R + 2) * s), (int)(ARC_CY - (ARC_R + 2) * c),
                 (int)(ARC_CX + (ARC_R - 8) * s), (int)(ARC_CY - (ARC_R - 8) * c), C_BLACK);
    }
    if (cpl_motion_playing()) gfx_text_c(ARC_CX, 54, F_SMALL, 1, C_BLACK, "LANE");
}

#define SC_X 17
#define SC_Y 21
#define SC_W 29
#define SC_H 21
static void draw_scope(void)
{
    int16_t s[SC_W * 2];
    uint32_t n = pump_scope(s, SC_W * 2), i;
    int py = SC_Y + SC_H / 2;
    gfx_fill(SC_X, SC_Y, SC_W, SC_H, C_WHITE);
    for (i = 0; i + 1 < n; i += 2) {
        int y = SC_Y + SC_H / 2 - (s[i] * (SC_H / 2 - 1)) / 32768;
        int x = SC_X + (int)(i / 2);
        if (i) gfx_line(x - 1, py, x, y, C_BLACK);
        py = y;
    }
}

static char status_sig[256];
static void draw_status(void)
{
    char b[256], code[13], song[PROJECT_NAME_LEN + 1];
    uint8_t p = SH->cur;
    int32_t ofs = crank_cut_offset_q8();
    int i, x;
    static const char *const cap[5] = { "MODE", "PATTERN", "GENERATION", "NEXT EVO", "TEMPO" };
    char val[5][12];

    corr_code_encode(&SH->proj.gen, code);
    memcpy(song, SH->proj.name, PROJECT_NAME_LEN);
    song[PROJECT_NAME_LEN] = 0;
    for (i = PROJECT_NAME_LEN - 1; i >= 0 && song[i] == ' '; i--) song[i] = 0;
    snprintf(val[0], 12, "%s", corr_mode_names[SH->proj.gen.mode]);
    snprintf(val[1], 12, "%02u", p + 1);
    snprintf(val[2], 12, "%u", SH->proj.pat[p].gen);
    if (bars_to_evolution(SH)) snprintf(val[3], 12, "%u", bars_to_evolution(SH));
    else snprintf(val[3], 12, "-");
    snprintf(val[4], 12, "%u", SH->proj.synth.bpm);

    snprintf(b, sizeof b, "%s|%s|%s|%s|%s|%s|%s|%s|%d|%d|%d|%u|%u|%u|%u|%u|%u|%u|%u", song, code,
             val[0], val[1], val[2], val[3], val[4], cpl_ext_code(),
             (int)crank_cut_now_q8 >> 8, (int)ofs >> 8, (int)crank_res_now_q8 >> 8,
             SH->playing, ui_fill_on, cur, cpl_rec_state(), SH->step >> 2, SH->bar_total,
             cpl_speaker_on(), SH->queued);
    if (!strcmp(b, status_sig) && !status_dirty) return;
    snprintf(status_sig, sizeof status_sig, "%s", b);
    status_dirty = 0;

    gfx_fill(64, 0, GFX_W - 64, 63, C_WHITE);
    /* header: name | song ... code */
    x = gfx_text(68, 2, F_MAIN, 1, C_BLACK, "ETCH CRANK");
    x = gfx_text(x + 4, 2, F_MAIN, 1, C_BLACK, "|");
    gfx_text(x + 4, 2, F_MAIN, 1, C_BLACK, song);
    if (*cpl_ext_code())
        snprintf(b, sizeof b, "%.4s-%.4s-%.4s-%s", code, code + 4, code + 8, cpl_ext_code());
    else
        snprintf(b, sizeof b, "%.4s-%.4s-%.4s", code, code + 4, code + 8);
    gfx_text_r(398, 2, F_MAIN, 1, C_BLACK, b);
    gfx_hline(68, 11, 330, C_D50);
    /* the readout strip */
    for (i = 0; i < 5; i++) {
        int cx = 68 + i * 66;
        gfx_text(cx, 14, F_SMALL, 1, C_BLACK, cap[i]);
        gfx_text(cx, 21, F_MAIN, 2, C_BLACK, val[i]);
        if (i) gfx_vline(cx - 4, 14, 22, C_D50);
    }
    /* crank, transport */
    x = gfx_textf(68, 39, F_MAIN, 1, C_BLACK, "CUT %d %s%d", (int)((crank_cut_now_q8 + 128) >> 8),
                  ofs >= 0 ? "+" : "", (int)((ofs + (ofs >= 0 ? 128 : -128)) / 256));
    x = gfx_textf(x + 10, 39, F_MAIN, 1, C_BLACK, "RES %d", (int)((crank_res_now_q8 + 128) >> 8));
    x = gfx_text(x + 10, 39, F_MAIN, 1, C_BLACK, SH->playing ? "PLAY" : "STOP");
    if (SH->playing) gfx_textf(x + 6, 39, F_MAIN, 1, C_BLACK, "%u.%u", (unsigned)SH->bar_total % 100 + 1, (SH->step >> 2) + 1);
    x = 300;
    if (ui_fill_on) { gfx_fill(x, 38, 26, 9, C_BLACK); gfx_text(x + 3, 39, F_MAIN, 1, C_WHITE, "FILL"); }
    if (cpl_rec_state()) {
        gfx_fill(x + 30, 38, 24, 9, cpl_rec_state() == 2 ? C_BLACK : C_D50);
        gfx_text(x + 33, 39, F_MAIN, 1, C_WHITE, "REC");
    }
    if (cpl_speaker_on()) gfx_text_r(398, 39, F_SMALL, 1, C_BLACK, "SPK");
    /* the screen row */
    gfx_fill(0, 50, GFX_W, 13, C_WHITE);
    for (i = 0; i < SCR_COUNT; i++) {
        int cx = 33 + i * 67, w = gfx_text_w(F_MAIN, 1, screen_names[i]);
        gfx_text(cx - w / 2, 52, F_MAIN, 1, C_BLACK, screen_names[i]);
        if (i == cur) gfx_fill(cx - w / 2 - 1, 60, w + 2, 2, C_BLACK);
    }
    gfx_hline(0, 63, GFX_W, C_BLACK);
}

/* ---- the hint line ------------------------------------------------------------------ */
static char hint_drawn[80];
static void draw_hint(void)
{
    char b[80];
    if (msg[0] && ui_now_ms < msg_until) snprintf(b, sizeof b, "%s", msg);
    else snprintf(b, sizeof b, "%s", screens[cur]->hint ? screens[cur]->hint() : "");
    if (!strcmp(b, hint_drawn)) return;
    snprintf(hint_drawn, sizeof hint_drawn, "%s", b);
    gfx_fill(0, HINT_Y - 2, GFX_W, GFX_H - HINT_Y + 2, C_WHITE);
    gfx_hline(0, HINT_Y - 2, GFX_W, C_D50);
    gfx_text(4, HINT_Y + 1, F_MAIN, 1, C_BLACK, b);
}

/* ---- the body: redraw when what it shows changed --------------------------------- */
static uint32_t watch_sig, refresh_ms;
static uint32_t watch(void)
{
    const uint8_t *p = (const uint8_t *)&SH->proj.pat[SH->cur];
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof(pattern_t); i++) h = (h ^ p[i]) * 16777619u;
    h = (h ^ SH->step) * 16777619u;
    h = (h ^ SH->cur) * 16777619u;
    h = (h ^ SH->playing) * 16777619u;
    h = (h ^ SH->evo_commits) * 16777619u;
    h = (h ^ SH->proj.mutes) * 16777619u;
    h = (h ^ SH->chain_pos) * 16777619u;
    return h;
}

static int32_t arc_cut = -1, arc_res = -1;
static uint8_t arc_stored = 0xFF, arc_lane = 0xFF;

void screen_init(void)
{
    gfx_clear(C_WHITE);
    cur = SCR_LIVE;
    if (screens[cur]->enter) screens[cur]->enter();
    body_dirty = status_dirty = 1;
}

void screen_frame(const ui_in_t *in)
{
    uint32_t w;
    ui_now_ms = in->now_ms;
    input(in);
    w = watch();
    if (w != watch_sig) { watch_sig = w; body_dirty = 1; }
    if (in->now_ms - refresh_ms >= 250) { refresh_ms = in->now_ms; body_dirty = 1; }
    if (crank_cut_now_q8 != arc_cut || crank_res_now_q8 != arc_res || crank_ui.stored_cut != arc_stored ||
        cpl_motion_playing() != arc_lane) {
        arc_cut = crank_cut_now_q8; arc_res = crank_res_now_q8; arc_stored = crank_ui.stored_cut;
        arc_lane = cpl_motion_playing();
        draw_arc();
    }
    draw_scope();
    draw_status();
    if (body_dirty) {
        gfx_fill(0, BODY_Y - 1, GFX_W, BODY_H + 1, C_WHITE);
        screens[cur]->draw();
        body_dirty = 0;
    }
    draw_hint();
}
