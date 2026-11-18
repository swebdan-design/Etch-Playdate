/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * SONG: tempo and the arrangement.  ETCH's SONG screen:
 *
 *   SONG MODE   A toggles: ON follows the chain, OFF loops the pattern
 *   BPM         LEFT / RIGHT +-1, A + UP / DOWN +-10
 *   SWING       0..8
 *   chain       16 slots: LEFT / RIGHT moves; A + LEFT / RIGHT sets the
 *               pattern (or END), A + UP / DOWN the bars (1-8); A tap jumps
 *               there at the next bar (in loop mode, queues the pattern);
 *               B + A sets END
 *   COPY        A + D-pad picks the destination, A copies the playing pattern
 *   CLEAR       A, then A again, clears the playing pattern (undoable)
 */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "gfx1.h"
#include "app_glue.h"
#include "audio.h"
#include "corrosion.h"
#include "cpl.h"

static int g_song(void) { return SH->proj.chain.song; }
static void s_song(int v) { app_song((uint8_t)v); }
static void a_song(void) { app_song((uint8_t)!SH->proj.chain.song); }
static int g_bpm(void) { return SH->proj.synth.bpm; }
static void s_bpm(int v)
{
    SH->proj.synth.bpm = (uint8_t)v;
    base_fx.bpm = (uint8_t)v;
    fx_p.bpm = (uint8_t)v;
    app_send(CMD(CMD_TEMPO, 0));
}
static int g_swing(void) { return SH->proj.synth.swing; }
static void s_swing(int v) { SH->proj.synth.swing = (uint8_t)v; app_send(CMD(CMD_TEMPO, 0)); }
static void f_onoff(int v, char *o) { strcpy(o, v ? "ON" : "OFF"); }
static const row_t song_rows[] = {
    { "SONG MODE", g_song, s_song, 0, 1, 1, 0, f_onoff, a_song },
    { "BPM", g_bpm, s_bpm, 60, 220, 10, 124, NULL, NULL },
    { "SWING", g_swing, s_swing, 0, 8, 1, 0, NULL, NULL },
};
static list_t slist = { song_rows, 3, 0, 0, 0 };

enum { A_LIST, A_CHAIN, A_COPY, A_CLEAR };
static uint8_t area = A_LIST, slot, copy_to = 1, clear_armed;

static void song_event(const ui_ev_t *e)
{
    chain_t *c = &SH->proj.chain;
    if (area == A_LIST) {
        if (e->nav_y > 0 && slist.sel == 2) { area = A_CHAIN; return; }
        if (e->a_tap && slist.sel == 0) { a_song(); return; }
        list_event(&slist, e);
        return;
    }
    if (e->nav_y) {
        if (area == A_CHAIN) {
            if (e->nav_y < 0) { if (slot >= 8) slot = (uint8_t)(slot - 8); else area = A_LIST; }
            else { if (slot < 8) slot = (uint8_t)(slot + 8); else area = A_COPY; }
        } else if (area == A_COPY) area = e->nav_y < 0 ? A_CHAIN : A_CLEAR;
        else if (e->nav_y < 0) area = A_COPY;
        clear_armed = 0;
        return;
    }
    if (area == A_CHAIN) {
        uint8_t p = c->pat[slot], r = c->rep[slot];
        if (e->nav_x) slot = (uint8_t)((slot + e->nav_x + 16) % 16);
        if (e->adj_x) {
            int np = p == 0xFF ? (e->adj_x > 0 ? 0 : 31) : p + e->adj_x;
            if (np < 0 || np > 31) np = 0xFF;
            app_chain(slot, (uint8_t)np, r ? r : 1);
        }
        if (e->adj_y) {
            int nr = (r ? r : 1) - e->adj_y;
            nr = nr < 1 ? 1 : nr > 8 ? 8 : nr;
            if (p != 0xFF) app_chain(slot, p, (uint8_t)nr);
        }
        if (e->del) app_chain(slot, 0xFF, 1);
        if (e->a_tap) {
            if (c->song) { app_jump(slot); ui_message("JUMP TO SLOT %u AT THE NEXT BAR", slot + 1); }
            else if (p != 0xFF) { app_queue(p); ui_message("PATTERN %02u FROM THE NEXT BAR", p + 1); }
        }
    } else if (area == A_COPY) {
        if (e->adj_x || e->adj_y || e->nav_x) copy_to = (uint8_t)((copy_to + e->adj_x - e->adj_y + e->nav_x + 32) % 32);
        if (e->a_tap) {
            app_replace_at(copy_to, &SH->proj.pat[SH->cur], 1);
            ui_message("PATTERN %02u COPIED TO %02u", SH->cur + 1, copy_to + 1);
        }
    } else {
        if (e->a_tap) {
            if (!clear_armed) { clear_armed = 1; ui_message("A AGAIN CLEARS PATTERN %02u", SH->cur + 1); }
            else {
                pattern_t p;
                pattern_clear(&p);
                app_replace(&p, 1);
                clear_armed = 0;
                ui_message("PATTERN %02u CLEARED (UNDO BRINGS IT BACK)", SH->cur + 1);
            }
        }
    }
}

static uint8_t song_can_ride(void) { return area == A_LIST && slist.sel > 0; }

static void song_draw(void)
{
    const chain_t *c = &SH->proj.chain;
    int i;
    list_draw(&slist, 8, 70, 384, 12, 3);
    gfx_text(8, 110, F_SMALL, 1, C_BLACK, "CHAIN  (PATTERN X BARS)");
    for (i = 0; i < 16; i++) {
        int x = 8 + (i % 8) * 48, y = 118 + (i / 8) * 30, sel = area == A_CHAIN && slot == i;
        int playing = c->song && SH->chain_pos == i;
        char b[8];
        gfx_fill(x, y, 44, 26, playing ? C_D25 : C_WHITE);
        gfx_frame(x, y, 44, 26, C_BLACK);
        gfx_textf(x + 2, y + 2, F_SMALL, 1, C_BLACK, "%u", i + 1);
        if (c->pat[i] == 0xFF) snprintf(b, sizeof b, "END");
        else snprintf(b, sizeof b, "%02u", c->pat[i] + 1);
        gfx_text(x + 4, y + 11, F_MAIN, 1, C_BLACK, b);
        if (c->pat[i] != 0xFF) gfx_textf(x + 28, y + 11, F_MAIN, 1, C_BLACK, "X%u", c->rep[i]);
        if (playing) gfx_fill(x + 2, y + 22, (40 * (SH->chain_bar + 1)) / (c->rep[i] ? c->rep[i] : 1), 2, C_BLACK);
        if (sel) gfx_frame(x - 2, y - 2, 48, 30, C_BLACK);
    }
    {
        int sel = area == A_COPY;
        if (sel) gfx_fill(6, 180, 190, 12, C_BLACK);
        gfx_textf(8, 182, F_MAIN, 1, sel ? C_WHITE : C_BLACK, "COPY PATTERN %02u TO %02u", SH->cur + 1, copy_to + 1);
        sel = area == A_CLEAR;
        if (sel) gfx_fill(6, 196, 190, 12, C_BLACK);
        gfx_textf(8, 198, F_MAIN, 1, sel ? C_WHITE : C_BLACK, clear_armed ? "CLEAR PATTERN %02u?  A AGAIN" : "CLEAR PATTERN %02u", SH->cur + 1);
    }
    gfx_textf(220, 182, F_MAIN, 1, C_BLACK, "%s", c->song ? "FOLLOWING THE CHAIN" : "LOOPING THE PATTERN");
}

static const char *song_hint(void)
{
    if (area == A_CHAIN) return "A+L/R PATTERN   A+U/D BARS   A JUMP   B+A END";
    if (area == A_COPY) return "A+D-PAD WHERE   A COPY";
    if (area == A_CLEAR) return "A, A CLEARS (UNDO ON LIVE OR GEN)";
    return "L/R OR A+D-PAD CHANGE   A TOGGLE SONG   A+CRANK RIDE";
}

static void song_enter(void) { clear_armed = 0; }

const screen_t scr_song = { "SONG", song_event, song_draw, song_can_ride, song_hint, song_enter };
