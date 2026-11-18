/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * TRACK: ETCH's tracker step editor.  One row per 16th, every channel in view:
 *
 *    #  NOT  A S L  K S H O  P  CD
 *
 *   NOT       A on an empty step writes the last note; A on a note turns it
 *             into a tie (===) and back; A + LEFT / RIGHT a semitone, A + UP /
 *             DOWN an octave; B + A rest
 *   A S L     accent / slide / step lock: A toggles, B + A clears
 *   K S H O   kick / snare / closed hat / open hat: A toggles, B + A clears
 *   P         chance 1-8 (A + D-pad); B + A sets 8
 *   CD        condition -- 12 22 14 24 34 44 FI NF (A + D-pad); B + A sets --
 *   #         A + UP / DOWN jumps a beat (ETCH: B + UP / DOWN, which is play
 *             and crank recording on the Playdate)
 *   PATTERN   (the header) A + LEFT / RIGHT: previous / next pattern, from
 *             the next bar (ETCH: B + LEFT / RIGHT, which change screens here)
 *   PAGE      (the header, RIGHT of PATTERN) A + LEFT / RIGHT: ETCH's columns or
 *             the ETCH DS layers:
 *
 *    #  RST  C A T  M N  RK RS RH RO
 *
 *   RST       RUST's note (as NOT; A toggles it on / off)
 *   C A T     chord (3-note stab in PARA 3) / accent / tie into the next step
 *   M N       METAL / NOISE hits
 *   RK..RO    ratchets for KICK SNARE HAT OPEN: 1-4 hits (A + D-pad), B + A 1
 *   A hand edit locks the pattern's layer part (the answering generator leaves it)
 */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "gfx1.h"
#include "app_glue.h"
#include "audio.h"
#include "corrosion.h"
#include "layers.h"

#define T_Y   78
#define T_RH  9
enum { C_NUM, C_NOT, C_A, C_S, C_L, C_K, C_SN, C_H, C_O, C_P, C_CD, C_COUNT };
static const int col_x[C_COUNT] = { 4, 22, 58, 70, 82, 104, 116, 128, 140, 162, 180 };
static const char *const cond_names[COND_COUNT] = { "--", "12", "22", "14", "24", "34", "44", "FI", "NF" };
static int8_t trow = 0, tcol = C_NOT;           /* trow -1 = the PATTERN header */
static uint8_t last_note = 24, last_rust = 36, page, hfield;

/* the layers page */
enum { LC_NUM, LC_RST, LC_C, LC_A, LC_T, LC_M, LC_N, LC_RK, LC_RS, LC_RH, LC_RO, LC_COUNT };
static const int lcol_x[LC_COUNT] = { 4, 22, 58, 70, 82, 104, 116, 138, 156, 174, 192 };

static void layers_event(const ui_ev_t *e, uint8_t s)
{
    layer_pat_t *L = &LD->pat[SH->cur & 31];
    uint8_t changed = 0, f = L->flags[s];
    switch (tcol) {
    case LC_NUM: if (e->adj_y) trow = (int8_t)((trow + e->adj_y * 4 + 16) % 16); return;
    case LC_RST:
        if (e->del && (f & RF_ON)) { L->flags[s] = 0; changed = 1; }
        else if (e->a_tap) { if (f & RF_ON) L->flags[s] = 0; else { L->note[s] = last_rust; L->flags[s] = RF_ON; } changed = 1; }
        else if (e->adj_x || e->adj_y) {
            int nn = ((f & RF_ON) ? L->note[s] : last_rust) + e->adj_x - e->adj_y * 12;
            nn = nn < 12 ? 12 : nn > 59 ? 59 : nn;
            last_rust = (uint8_t)nn;
            L->note[s] = (uint8_t)nn;
            L->flags[s] |= RF_ON;
            changed = 1;
        }
        break;
    case LC_C: case LC_A: case LC_T: {
        uint8_t bit = tcol == LC_C ? RF_CHORD : tcol == LC_A ? RF_ACCENT : RF_TIE;
        if (e->a_tap) { L->flags[s] ^= bit; changed = 1; }
        if (e->del && (f & bit)) { L->flags[s] &= (uint8_t)~bit; changed = 1; }
        break;
    }
    case LC_M: case LC_N: {
        uint16_t *m = tcol == LC_M ? &L->metal : &L->noise;
        if (e->a_tap || (e->del && ((*m >> s) & 1))) { *m ^= (uint16_t)(1u << s); changed = 1; }
        break;
    }
    default: {
        int lane = tcol - LC_RK, v = (L->ratchet[s] >> (lane * 2)) & 3;
        if (e->del) v = 0;
        v += e->adj_x - e->adj_y;
        v = v < 0 ? 0 : v > 3 ? 3 : v;
        if (v != ((L->ratchet[s] >> (lane * 2)) & 3) || e->del) {
            L->ratchet[s] = (uint8_t)((L->ratchet[s] & ~(3u << (lane * 2))) | ((unsigned)v << (lane * 2)));
            changed = 1;
        }
    }
    }
    if (changed) { L->lock = 1; LD->manual = 1; }
}

static void layers_draw_rows(void)
{
    const layer_pat_t *L = &LD->pat[SH->cur & 31];
    int r, c;
    char b[8];
    for (r = 0; r < 16; r++) {
        int y = T_Y + r * T_RH;
        uint8_t f = L->flags[r];
        if (SH->playing && SH->step == r) gfx_fill(2, y - 1, 214, T_RH, C_D25);
        if ((r & 3) == 0) gfx_hline(2, y - 1, 214, C_D50);
        for (c = 0; c < LC_COUNT; c++) {
            const char *txt = ".";
            switch (c) {
            case LC_NUM: snprintf(b, sizeof b, "%X", r); txt = b; break;
            case LC_RST: txt = (f & RF_ON) ? note_name(L->note[r]) : "---"; break;
            case LC_C: txt = (f & RF_CHORD) ? "C" : "."; break;
            case LC_A: txt = (f & RF_ACCENT) ? "A" : "."; break;
            case LC_T: txt = (f & RF_TIE) ? "T" : "."; break;
            case LC_M: txt = ((L->metal >> r) & 1) ? "M" : "."; break;
            case LC_N: txt = ((L->noise >> r) & 1) ? "N" : "."; break;
            default: snprintf(b, sizeof b, "%d", ((L->ratchet[r] >> ((c - LC_RK) * 2)) & 3) + 1); txt = b; break;
            }
            if (trow == r && tcol == c) {
                int w = gfx_text_w(F_MAIN, 1, txt);
                gfx_fill(lcol_x[c] - 2, y - 1, (w < 6 ? 6 : w) + 4, T_RH, C_BLACK);
                gfx_text(lcol_x[c], y, F_MAIN, 1, C_WHITE, txt);
            } else gfx_text(lcol_x[c], y, F_MAIN, 1, C_BLACK, txt);
        }
    }
}

static void replace_prob(uint8_t step, uint8_t prob)
{
    pattern_t p;
    memcpy(&p, &SH->proj.pat[SH->cur], sizeof p);
    p.prob[step] = prob;
    app_replace(&p, 0);
}

static void track_event(const ui_ev_t *e)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    uint8_t s;
    if (e->nav_y) { trow = (int8_t)(trow + e->nav_y); if (trow < -1) trow = -1; if (trow > 15) trow = 15; }
    if (trow < 0) {                                  /* the header: PATTERN, PAGE */
        if (e->nav_x) hfield = (uint8_t)(e->nav_x > 0);
        if (hfield == 0 && (e->adj_x || e->adj_y)) {
            int np = ((SH->queued != 0xFF ? SH->queued : SH->cur) + e->adj_x - e->adj_y + 32) % 32;
            app_queue((uint8_t)np);
        }
        if (hfield == 1 && (e->adj_x || e->adj_y || e->a_tap)) { page ^= 1; tcol = 1; }
        return;
    }
    if (e->nav_x) {
        int n = page ? LC_COUNT : C_COUNT;
        tcol = (int8_t)(tcol + e->nav_x);
        if (tcol < 0) tcol = 0;
        if (tcol >= n) tcol = (int8_t)(n - 1);
    }
    s = (uint8_t)trow;
    if (page) { layers_event(e, s); return; }
    switch (tcol) {
    case C_NUM:
        if (e->adj_y) { trow = (int8_t)((trow + e->adj_y * 4 + 16) % 16); }
        break;
    case C_NOT: {
        uint8_t f = p->flags[s], t = f & STEP_TYPE_MASK, n = p->note[s];
        if (e->del) app_set_bass(s, n, (uint8_t)(f & ~STEP_TYPE_MASK & ~STEP_SLIDE));
        else if (e->a_tap) {
            if (t == STEP_NOTE) app_set_bass(s, n, (uint8_t)((f & ~STEP_TYPE_MASK) | STEP_TIE));
            else if (t == STEP_TIE) app_set_bass(s, n, (uint8_t)((f & ~STEP_TYPE_MASK) | STEP_NOTE));
            else app_set_bass(s, last_note, (uint8_t)((f & ~STEP_TYPE_MASK) | STEP_NOTE));
        } else if (e->adj_x || e->adj_y) {
            int nn = (t == STEP_NOTE ? n : last_note) + e->adj_x - e->adj_y * 12;
            nn = nn < NOTE_MIN ? NOTE_MIN : nn > NOTE_MAX ? NOTE_MAX : nn;
            last_note = (uint8_t)nn;
            app_set_bass(s, (uint8_t)nn, (uint8_t)((f & ~STEP_TYPE_MASK) | STEP_NOTE));
            if (!SH->playing) { live_note(EV_NOTE, (uint8_t)nn, 0); live_note(EV_GATE_OFF, 0, 0); }
        }
        break;
    }
    case C_A: case C_S: case C_L: {
        uint8_t bit = tcol == C_A ? STEP_ACCENT : tcol == C_S ? STEP_SLIDE : STEP_LOCK, f = p->flags[s];
        if (e->a_tap) app_set_bass(s, p->note[s], (uint8_t)(f ^ bit));
        if (e->del && (f & bit)) app_set_bass(s, p->note[s], (uint8_t)(f & ~bit));
        break;
    }
    case C_K: case C_SN: case C_H: case C_O: {
        uint8_t lane = (uint8_t)(tcol - C_K), on = (p->drum[lane] >> s) & 1;
        if (e->a_tap || (e->del && on)) app_toggle_drum(lane, s);
        break;
    }
    case C_P: {
        int v = p->prob[s] & 7;
        if (e->del) v = PROB_ALWAYS;
        v += e->adj_x - e->adj_y;
        v = v < 0 ? 0 : v > 7 ? 7 : v;
        if (v != (p->prob[s] & 7)) replace_prob(s, (uint8_t)((p->prob[s] & 0xF0) | v));
        break;
    }
    case C_CD: {
        int v = p->prob[s] >> 4;
        if (e->del) v = COND_ALL;
        v = (v + e->adj_x - e->adj_y + COND_COUNT) % COND_COUNT;
        if (v != (p->prob[s] >> 4)) replace_prob(s, (uint8_t)((p->prob[s] & 0x0F) | (v << 4)));
        break;
    }
    }
}

static void track_draw(void)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    int r, c;
    char b[48];
    /* header */
    {
        int sel = trow < 0;
        uint8_t q = SH->queued;
        if (q != 0xFF && q != SH->cur) snprintf(b, sizeof b, "PATTERN %02u>%02u", SH->cur + 1, q + 1);
        else snprintf(b, sizeof b, "PATTERN %02u", SH->cur + 1);
        if (sel && hfield == 0) gfx_fill(2, 66, gfx_text_w(F_MAIN, 1, b) + 4, 10, C_BLACK);
        gfx_text(4, 67, F_MAIN, 1, sel && hfield == 0 ? C_WHITE : C_BLACK, b);
        snprintf(b, sizeof b, "PAGE %s", page ? "LAYERS" : "ETCH");
        if (sel && hfield == 1) gfx_fill(118, 66, gfx_text_w(F_MAIN, 1, b) + 4, 10, C_BLACK);
        gfx_text(120, 67, F_MAIN, 1, sel && hfield == 1 ? C_WHITE : C_BLACK, b);
    }
    if (page) {
        const layer_pat_t *L = &LD->pat[SH->cur & 31];
        layers_draw_rows();
        gfx_vline(222, 66, 158, C_D50);
        gfx_text(230, 70, F_SMALL, 1, C_BLACK, "COLUMNS");
        gfx_text(230, 79, F_MAIN, 1, C_BLACK, "# RST C A T  M N  RK RS RH RO");
        gfx_textf(230, 96, F_MAIN, 1, C_BLACK, "LENGTHS  RUST %u  METAL %u  NOISE %u", L->rust_len, L->metal_len, L->noise_len);
        gfx_textf(230, 108, F_MAIN, 1, C_BLACK, "LAYERS %s%s", layers_active() ? "PLAYING" : "OFF", L->lock ? "  LOCKED" : "");
        gfx_wrap(230, 124, 166, F_MAIN, 10, C_BLACK, layers_active() ? "RUST, METAL AND NOISE ANSWER CORROSION'S BASS AND DRUMS. A HAND EDIT LOCKS THIS PATTERN'S LAYERS."
                 : "THE LAYERS PLAY WITH A 4TH CODE GROUP (GEN: ANSWER) OR ONCE YOU WRITE ONE BY HAND.");
        return;
    }
    for (r = 0; r < 16; r++) {
        int y = T_Y + r * T_RH;
        uint8_t f = p->flags[r], t = f & STEP_TYPE_MASK;
        if (SH->playing && SH->step == r) gfx_fill(2, y - 1, 214, T_RH, C_D25);
        if ((r & 3) == 0) gfx_hline(2, y - 1, 214, C_D50);
        for (c = 0; c < C_COUNT; c++) {
            const char *txt = "";
            int on = 0;
            switch (c) {
            case C_NUM: snprintf(b, sizeof b, "%X", r); txt = b; break;
            case C_NOT: txt = t == STEP_NOTE ? note_name(p->note[r]) : t == STEP_TIE ? "===" : "---"; break;
            case C_A: on = f & STEP_ACCENT; txt = on ? "A" : "."; break;
            case C_S: on = f & STEP_SLIDE; txt = on ? "S" : "."; break;
            case C_L: on = f & STEP_LOCK; txt = on ? "L" : "."; break;
            case C_K: case C_SN: case C_H: case C_O:
                on = (p->drum[c - C_K] >> r) & 1;
                txt = on ? (c == C_K ? "K" : c == C_SN ? "S" : c == C_H ? "H" : "O") : ".";
                break;
            case C_P: snprintf(b, sizeof b, "%d", (p->prob[r] & 7) + 1); txt = b; break;
            case C_CD: txt = cond_names[(p->prob[r] >> 4) % COND_COUNT]; break;
            }
            if (trow == r && tcol == c) {
                int w = gfx_text_w(F_MAIN, 1, txt);
                gfx_fill(col_x[c] - 2, y - 1, (w < 6 ? 6 : w) + 4, T_RH, C_BLACK);
                gfx_text(col_x[c], y, F_MAIN, 1, C_WHITE, txt);
            } else gfx_text(col_x[c], y, F_MAIN, 1, C_BLACK, txt);
        }
    }
    /* titles and the side panel */
    gfx_vline(222, 66, 158, C_D50);
    {
        int y = 70;
        gfx_text(230, y, F_SMALL, 1, C_BLACK, "COLUMNS");
        y += 9;
        gfx_text(230, y, F_MAIN, 1, C_BLACK, "# NOT  A S L  K S H O  P CD"); y += 14;
        if (trow >= 0) {
            uint8_t f = p->flags[trow], t = f & STEP_TYPE_MASK;
            gfx_textf(230, y, F_MAIN, 2, C_BLACK, "STEP %X", trow); y += 18;
            gfx_textf(230, y, F_MAIN, 1, C_BLACK, "%s %s%s%s", t == STEP_NOTE ? note_name(p->note[trow]) : t == STEP_TIE ? "TIE" : "REST",
                      f & STEP_ACCENT ? "ACC " : "", f & STEP_SLIDE ? "SLIDE " : "", f & STEP_LOCK ? "LOCK" : ""); y += 11;
            gfx_textf(230, y, F_MAIN, 1, C_BLACK, "CHANCE %d/8", (p->prob[trow] & 7) + 1); y += 11;
            {
                static const char *const cl[COND_COUNT] = { "EVERY LOOP", "1ST OF 2", "2ND OF 2", "1ST OF 4", "2ND OF 4",
                                                            "3RD OF 4", "4TH OF 4", "ONLY IN FILLS", "NEVER IN FILLS" };
                gfx_textf(230, y, F_MAIN, 1, C_BLACK, "%s", cl[(p->prob[trow] >> 4) % COND_COUNT]); y += 11;
            }
        }
        y += 6;
        gfx_textf(230, y, F_SMALL, 1, C_BLACK, "LOCKS %s%s", p->lock & PLOCK_ALL ? "PATTERN " : "", p->lock & PLOCK_BASS ? "BASS" : "");
    }
}

static const char *track_hint(void)
{
    if (trow < 0) return hfield ? "A+L/R OR A: ETCH / LAYERS" : "A+L/R PATTERN (FROM THE NEXT BAR)   RIGHT: PAGE";
    if (page) {
        if (tcol == LC_RST) return "A NOTE ON/OFF   A+L/R SEMI   A+U/D OCT   B+A OFF";
        if (tcol >= LC_RK) return "A+D-PAD RATCHET 1-4   B+A 1";
        if (tcol == LC_NUM) return "A+U/D JUMP A BEAT";
        return "A ON/OFF   B+A CLEAR";
    }
    switch (trow < 0 ? -1 : tcol) {
    case -1: return "A+L/R PATTERN (FROM THE NEXT BAR)";
    case C_NUM: return "A+U/D JUMP A BEAT";
    case C_NOT: return "A NOTE/TIE   A+L/R SEMI   A+U/D OCT   B+A REST";
    case C_P: return "A+D-PAD CHANCE   B+A ALWAYS";
    case C_CD: return "A+D-PAD CONDITION   B+A EVERY LOOP";
    default: return "A ON/OFF   B+A CLEAR";
    }
}

static uint8_t track_can_ride(void) { return 0; }
static void track_enter(void) { }

const screen_t scr_track = { "TRACK", track_event, track_draw, track_can_ride, track_hint, track_enter };
