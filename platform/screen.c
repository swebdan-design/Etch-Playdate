/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * Milestone 1's screen (see screen.h).  1-bit, 400 x 240, redrawn by region
 * only when what a region shows has changed.
 *
 *   y   0- 63  status band: the cutoff arc (live value as a filled arc, the
 *              stored value as a tick, resonance as the inner arc) around the
 *              scope; name, song, seed code; MODE, NEXT EVO, GENERATION,
 *              song position, tempo; the live cutoff and resonance
 *   y  66-153  LIVE: BASS and ETCH's four drum lanes, 16 steps, playhead.
 *              Steps CORROSION has just changed start solid and fade through
 *              75 / 50 / 25 % dither over two bars (the DS's glow and fade)
 *   y 156-239  the CPU meter, per stage, and the hint line
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "screen.h"
#include "app_glue.h"
#include "audio.h"
#include "corrosion.h"
#include "pump.h"
#include "crank.h"

static PlaydateAPI *pd;
static LCDFont *font;

uint32_t main_menu_callbacks(void);

/* 1 = white on the Playdate's display; the masks are opaque */
static LCDPattern dither75 = { 0x55, 0x00, 0xAA, 0x00, 0x55, 0x00, 0xAA, 0x00,
                               0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static LCDPattern dither50 = { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
                               0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static LCDPattern dither25 = { 0xAA, 0xFF, 0x55, 0xFF, 0xAA, 0xFF, 0x55, 0xFF,
                               0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

#define BLACK ((LCDColor)kColorBlack)
#define WHITE ((LCDColor)kColorWhite)

static void text(const char *s, int x, int y)
{
    pd->graphics->drawText(s, strlen(s), kASCIIEncoding, x, y);
}

static void clear(int x, int y, int w, int h) { pd->graphics->fillRect(x, y, w, h, WHITE); }

/* ---- CPU clock calibration ----------------------------------------------------- */
static uint32_t cal_t0, cal_ms0;
static double tick_hz = 168e6;            /* until measured */

static void calibrate(void)
{
    uint32_t ms = pd->system->getCurrentTimeMilliseconds();
    uint32_t t = host_clock ? host_clock() : 0;
    if (!cal_ms0) { cal_ms0 = ms; cal_t0 = t; return; }
    if (ms - cal_ms0 >= 2000) {
        double hz = (double)(uint32_t)(t - cal_t0) * 1000.0 / (double)(ms - cal_ms0);
        if (hz > 1e6) tick_hz = hz;
        cal_ms0 = ms;
        cal_t0 = t;
    }
}

/* render.arm.c's percent x 100, which assumes 131 072 ticks per block,
 * rescaled to the measured tick rate: percent of real time */
static double real_pct(uint32_t v)
{
    double ticks_per_block = tick_hz * AUDIO_BLOCK / 32728.498;
    return (double)v / 100.0 * 131072.0 / ticks_per_block;
}

/* ---- status band --------------------------------------------------------------- */
#define ARC_X 4
#define ARC_Y 4
#define ARC_D 56
#define ARC_START 210.0f
#define ARC_SPAN 300.0f

static float arc_angle(int32_t q8)
{
    float a = ARC_START + ARC_SPAN * (float)q8 / (float)CRANK_MAX_Q8;
    return a >= 360.0f ? a - 360.0f : a;
}

static void draw_arc(void)
{
    float cx = ARC_X + ARC_D / 2.0f, cy = ARC_Y + ARC_D / 2.0f, a, r0, r1;
    int32_t cut = crank_cut_now_q8, res = crank_res_now_q8;
    clear(0, 0, 66, 64);
    /* the track, the live cutoff, the resonance inside it */
    pd->graphics->drawEllipse(ARC_X, ARC_Y, ARC_D, ARC_D, 1, ARC_START, arc_angle(CRANK_MAX_Q8), BLACK);
    if (cut > 0)
        pd->graphics->drawEllipse(ARC_X, ARC_Y, ARC_D, ARC_D, 5, ARC_START, arc_angle(cut), BLACK);
    if (res > 0)
        pd->graphics->drawEllipse(ARC_X + 7, ARC_Y + 7, ARC_D - 14, ARC_D - 14, 1, ARC_START, arc_angle(res), BLACK);
    /* the stored value: a tick across the track */
    a = (ARC_START + ARC_SPAN * (float)crank_ui.stored_cut / 127.0f) * 3.14159265f / 180.0f;
    r0 = ARC_D / 2.0f + 3.0f;
    r1 = ARC_D / 2.0f - 8.0f;
    pd->graphics->drawLine((int)(cx + r0 * sinf(a)), (int)(cy - r0 * cosf(a)),
                           (int)(cx + r1 * sinf(a)), (int)(cy - r1 * cosf(a)), 2, BLACK);
}

#define SCOPE_X 18
#define SCOPE_Y 21
#define SCOPE_W 30
#define SCOPE_H 22

static void draw_scope(void)
{
    int16_t s[SCOPE_W * 2];
    uint32_t n = pump_scope(s, SCOPE_W * 2), i;
    int py = SCOPE_Y + SCOPE_H / 2;
    clear(SCOPE_X, SCOPE_Y, SCOPE_W, SCOPE_H);
    for (i = 0; i + 1 < n && i < SCOPE_W * 2; i += 2) {
        int y = SCOPE_Y + SCOPE_H / 2 - (s[i] * (SCOPE_H / 2 - 1)) / 32768;
        int x = SCOPE_X + (int)(i / 2);
        if (i) pd->graphics->drawLine(x - 1, py, x, y, 1, BLACK);
        py = y;
    }
}

static char line_cache[10][96];
static void line(int k, int x, int y, int w, const char *s)
{
    if (!strcmp(line_cache[k], s)) return;
    snprintf(line_cache[k], sizeof line_cache[k], "%s", s);
    clear(x, y, w, 14);
    text(s, x, y);
}

static char msg[64];
static uint32_t msg_until;
void screen_message(const char *m)
{
    snprintf(msg, sizeof msg, "%s", m);
    msg_until = pd->system->getCurrentTimeMilliseconds() + 2500;
}

static uint8_t menu_is_open;
void screen_menu(uint8_t open)
{
    menu_is_open = open;
    if (!open) cal_ms0 = 0;    /* the game's clock stood still: measure afresh */
}

static void draw_status(uint8_t fill_on)
{
    char b[96], code[13], song[PROJECT_NAME_LEN + 1];
    uint8_t cur = SH->cur;
    int32_t ofs = crank_cut_offset_q8();
    corr_code_encode(&SH->proj.gen, code);
    memcpy(song, SH->proj.name, PROJECT_NAME_LEN);
    song[PROJECT_NAME_LEN] = 0;
    { int i; for (i = PROJECT_NAME_LEN - 1; i >= 0 && song[i] == ' '; i--) song[i] = 0; }
    snprintf(b, sizeof b, "ETCH CRANK  |  %s  |  %.4s-%.4s-%.4s", song, code, code + 4, code + 8);
    line(0, 72, 2, 328, b);
    snprintf(b, sizeof b, "%s  NEXT EVO %u  GEN %u  PAT %02u  BAR %u.%u",
             corr_mode_names[SH->proj.gen.mode], bars_to_evolution(SH), SH->proj.pat[cur].gen,
             cur + 1, (unsigned)SH->bar_total + 1u, (SH->step >> 2) + 1u);
    line(1, 72, 17, 328, b);
    snprintf(b, sizeof b, "CUT %u (%s%d)  RES %u  %u BPM  %s%s",
             (unsigned)((crank_cut_now_q8 + 128) >> 8), ofs >= 0 ? "+" : "", (int)((ofs + (ofs >= 0 ? 128 : -128)) / 256),
             (unsigned)((crank_res_now_q8 + 128) >> 8), SH->proj.synth.bpm, SH->playing ? "PLAY" : "STOP",
             fill_on ? "  FILL" : "");
    line(2, 72, 32, 328, b);
    if (msg[0] && pd->system->getCurrentTimeMilliseconds() < msg_until) snprintf(b, sizeof b, "%s", msg);
    else snprintf(b, sizeof b, "%s", crank_ui.docked ? "CRANK DOCKED: UNDOCK TO PLAY THE FILTER" : "CRANK CUTOFF   B+CRANK RES   DOCK: HOME");
    line(3, 72, 47, 328, b);
}

/* ---- LIVE: the pattern ---------------------------------------------------------- */
#define PAT_Y  66
#define ROW_H  16
#define CELL_X 40
#define CELL_W 22
static const char *const lane_names[5] = { "BASS", "KICK", "SNR", "HAT", "OPEN" };

static pattern_t seen;
static uint8_t seen_pat = 0xFF, seen_commits;
static uint32_t glow_t0[5][NUM_STEPS];     /* when the cell last changed (0 = never) */
static uint8_t glow_lvl_drawn[5][NUM_STEPS];
static uint8_t drawn_step = 0xFF, drawn_playing = 0xFF;
static pattern_t drawn;
static uint8_t drawn_valid;

static uint8_t glow_level(uint32_t t0, uint32_t now, uint32_t two_bars)
{
    uint32_t t;
    if (!t0) return 0;
    t = now - t0;
    if (t >= two_bars) return 0;
    return (uint8_t)(4 - (t * 4) / two_bars);   /* 4 solid, 3 75 %, 2 50 %, 1 25 % */
}

static void draw_cell(const pattern_t *p, int row, int s, uint8_t lvl)
{
    int x = CELL_X + s * CELL_W, y = PAT_Y + row * ROW_H;
    clear(x, y, CELL_W, ROW_H);
    if (lvl) {
        LCDColor c = lvl == 4 ? BLACK : (LCDColor)(lvl == 3 ? dither75 : lvl == 2 ? dither50 : dither25);
        pd->graphics->fillRect(x + 1, y + 1, CELL_W - 2, ROW_H - 2, c);
    }
    if (row == 0) {
        uint8_t t = p->flags[s] & STEP_TYPE_MASK;
        if (t == STEP_NOTE) {
            int h = (p->flags[s] & STEP_ACCENT) ? 12 : 8;
            pd->graphics->fillRect(x + 4, y + ROW_H - 2 - h, CELL_W - 8, h, BLACK);
            if (lvl) pd->graphics->drawRect(x + 3, y + ROW_H - 3 - h, CELL_W - 6, h + 2, WHITE);
        } else if (t == STEP_TIE) {
            pd->graphics->fillRect(x, y + ROW_H - 8, CELL_W, 3, BLACK);
        } else {
            pd->graphics->fillRect(x + CELL_W / 2 - 1, y + ROW_H / 2, 2, 2, lvl >= 3 ? WHITE : BLACK);
        }
        if (p->flags[s] & STEP_SLIDE) pd->graphics->drawLine(x + CELL_W - 4, y + 3, x + CELL_W, y + 3, 1, lvl >= 3 ? WHITE : BLACK);
    } else if ((p->drum[row - 1] >> s) & 1) {
        pd->graphics->fillRect(x + 5, y + 3, CELL_W - 10, ROW_H - 6, BLACK);
        if (lvl) pd->graphics->drawRect(x + 4, y + 2, CELL_W - 8, ROW_H - 4, WHITE);
    } else {
        pd->graphics->fillRect(x + CELL_W / 2 - 1, y + ROW_H / 2 - 1, 2, 2, lvl >= 3 ? WHITE : BLACK);
    }
}

static void draw_pattern(void)
{
    uint8_t cur = SH->cur, step = SH->step, playing = SH->playing;
    uint32_t now = pd->system->getCurrentTimeMilliseconds();
    uint32_t two_bars = 8u * 60000u / (SH->proj.synth.bpm ? SH->proj.synth.bpm : 120);
    const pattern_t *p = &SH->proj.pat[cur];
    int row, s, all = 0;

    /* steps CORROSION changed: compare with what we saw, after a commit */
    if (cur != seen_pat) {
        memcpy(&seen, p, sizeof seen);
        seen_pat = cur;
        seen_commits = SH->evo_commits;
        memset(glow_t0, 0, sizeof glow_t0);
        all = 1;
    } else if (memcmp(&seen, p, sizeof seen)) {
        uint8_t evolved = SH->evo_commits != seen_commits;
        for (s = 0; s < NUM_STEPS; s++) {
            if (seen.note[s] != p->note[s] || seen.flags[s] != p->flags[s])
                if (evolved) glow_t0[0][s] = now ? now : 1;
            for (row = 1; row < 5; row++)
                if (((seen.drum[row - 1] ^ p->drum[row - 1]) >> s) & 1)
                    if (evolved) glow_t0[row][s] = now ? now : 1;
        }
        memcpy(&seen, p, sizeof seen);
        seen_commits = SH->evo_commits;
    }

    if (!drawn_valid) {
        clear(0, PAT_Y - 2, 400, 5 * ROW_H + 10);
        for (row = 0; row < 5; row++) text(lane_names[row], 4, PAT_Y + row * ROW_H + 1);
        all = 1;
        drawn_valid = 1;
    }
    for (row = 0; row < 5; row++)
        for (s = 0; s < NUM_STEPS; s++) {
            uint8_t lvl = glow_level(glow_t0[row][s], now, two_bars);
            uint8_t diff = all || lvl != glow_lvl_drawn[row][s] ||
                           (row == 0 ? (drawn.note[s] != p->note[s] || drawn.flags[s] != p->flags[s])
                                     : (((drawn.drum[row - 1] ^ p->drum[row - 1]) >> s) & 1));
            if (diff) {
                draw_cell(p, row, s, lvl);
                glow_lvl_drawn[row][s] = lvl;
            }
        }
    memcpy(&drawn, p, sizeof drawn);

    /* the playhead, and beat ticks */
    if (all || step != drawn_step || playing != drawn_playing) {
        int y = PAT_Y + 5 * ROW_H + 2;
        clear(CELL_X, y, NUM_STEPS * CELL_W, 6);
        for (s = 0; s < NUM_STEPS; s += 4) pd->graphics->fillRect(CELL_X + s * CELL_W, y + 4, 1, 2, BLACK);
        if (playing) pd->graphics->fillRect(CELL_X + step * CELL_W + 2, y, CELL_W - 4, 4, BLACK);
        drawn_step = step;
        drawn_playing = playing;
    }
}

/* ---- CPU meter -------------------------------------------------------------------- */
static uint32_t meter_windows = 0xFFFFFFFFu;

static void draw_meter(void)
{
    char b[96];
    double cb_pct = 0, cb_worst_ms = 0, ms_per_cb = 0;
    if (astats.windows == meter_windows && !menu_is_open) return;
    meter_windows = astats.windows;
    if (pstats.frames_last) {
        double secs = pstats.frames_last / 44100.0;
        cb_pct = 100.0 * (double)pstats.cb_sum_last / (secs * tick_hz);
        ms_per_cb = 1000.0 * secs / 1.0;
    }
    cb_worst_ms = 1000.0 * (double)pstats.cb_max_last / tick_hz;
    (void)ms_per_cb;
    snprintf(b, sizeof b, "CPU %% OF REAL TIME  (CLOCK %.1f MHZ)", tick_hz / 1e6);
    line(4, 4, 158, 396, b);
    snprintf(b, sizeof b, "ACID %.1f  DRUMS %.1f  DRIVE %.1f  CRUSH %.1f  DELAY %.1f",
             real_pct(astats.avg_pct[ST_ACID]), real_pct(astats.avg_pct[ST_DRUMS]),
             real_pct(astats.avg_pct[ST_DRIVE]), real_pct(astats.avg_pct[ST_CRUSH]),
             real_pct(astats.avg_pct[ST_DELAY]));
    line(5, 4, 172, 396, b);
    snprintf(b, sizeof b, "COMB %.1f  LIMIT %.1f  EVENTS %.1f  SEQ %.1f  RESAMPLE %.1f",
             real_pct(astats.avg_pct[ST_COMB]), real_pct(astats.avg_pct[ST_LIMIT]),
             real_pct(astats.avg_pct[ST_EVENTS]), real_pct(pstats.seq_pct), real_pct(pstats.rs_pct));
    line(6, 4, 186, 396, b);
    snprintf(b, sizeof b, "ENGINE %.1f  CALLBACK %.1f  WORST BLK %.1f  WORST CB %.2f MS",
             real_pct(astats.avg_total), cb_pct, real_pct(astats.max_total_pct), cb_worst_ms);
    line(7, 4, 200, 396, b);
    {
        static char last[96];
        snprintf(b, sizeof b, "CALLBACKS %lu  IN MENU %lu  DROPS %lu  LATE %lu",
                 (unsigned long)pstats.callbacks, (unsigned long)main_menu_callbacks(),
                 (unsigned long)pump_cmd_drops, (unsigned long)SH->gates_late);
        if (strcmp(last, b)) {
            snprintf(last, sizeof last, "%s", b);
            clear(4, 214, 396, 14);
            text(b, 4, 214);
        }
    }
    line(3 + 5, 4, 226, 396, "B+DOWN PLAY/STOP   B FILL   B+DOCK KEEP");
    /* the log line, for a serial console on hardware */
    if ((meter_windows & 7) == 0)
        pd->system->logToConsole("CPU MHz %.1f acid %.2f drums %.2f drive %.2f crush %.2f delay %.2f comb %.2f limit %.2f "
                                 "events %.2f seq %.2f resample %.2f engine %.2f callback %.2f worstblock %.2f worstcb_ms %.3f drops %lu",
                                 tick_hz / 1e6, real_pct(astats.avg_pct[ST_ACID]), real_pct(astats.avg_pct[ST_DRUMS]),
                                 real_pct(astats.avg_pct[ST_DRIVE]), real_pct(astats.avg_pct[ST_CRUSH]),
                                 real_pct(astats.avg_pct[ST_DELAY]), real_pct(astats.avg_pct[ST_COMB]),
                                 real_pct(astats.avg_pct[ST_LIMIT]), real_pct(astats.avg_pct[ST_EVENTS]),
                                 real_pct(pstats.seq_pct), real_pct(pstats.rs_pct), real_pct(astats.avg_total),
                                 cb_pct, real_pct(astats.max_total_pct), cb_worst_ms, (unsigned long)pump_cmd_drops);
}

/* ---- frame ------------------------------------------------------------------------- */
static int32_t arc_drawn_cut = -1, arc_drawn_res = -1;
static uint8_t arc_drawn_stored = 0xFF;

void screen_init(PlaydateAPI *api)
{
    const char *err = NULL;
    pd = api;
    font = pd->graphics->loadFont("/System/Fonts/Roobert-10-Bold.pft", &err);
    if (font) pd->graphics->setFont(font);
    pd->graphics->clear(WHITE);
    pd->graphics->drawLine(0, 63, 399, 63, 1, BLACK);
    pd->graphics->drawLine(0, 155, 399, 155, 1, BLACK);
}

void screen_frame(uint8_t fill_on)
{
    calibrate();
    if (crank_cut_now_q8 != arc_drawn_cut || crank_res_now_q8 != arc_drawn_res ||
        crank_ui.stored_cut != arc_drawn_stored) {
        draw_arc();
        arc_drawn_cut = crank_cut_now_q8;
        arc_drawn_res = crank_res_now_q8;
        arc_drawn_stored = crank_ui.stored_cut;
    }
    draw_scope();
    draw_status(fill_on);
    draw_pattern();
    draw_meter();
}
