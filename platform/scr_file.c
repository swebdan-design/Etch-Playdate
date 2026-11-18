/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * FILE: projects, demo songs, saving, codes, about.  ETCH's FILE screen, with
 * CODE and DEMOS here rather than in the system menu (the Playdate's system
 * menu silences the audio, docs/MILESTONE1_REPORT.md):
 *
 *   NEW        A, A: an empty project (UNTITLED), then LIVE
 *   LOAD       ETCH's eight demo songs (D1-D8), the boot song (CORRODE from
 *              its code), and the 16 save slots.  A arms, A again loads; LIVE
 *              plays it.
 *   SAVE       the 16 slots.  A saves (A, A over a used slot); B + A, then A,
 *              erases a slot
 *   CODE       a project from a seed code: goes to GEN's code row
 *   IMPORT DS  ETCH DS saves (etch_ds.sav in the data folder): A, A
 *   IMPORT GB  ETCH Game Boy saves (etch_gb.sav): A, A
 *   ABOUT      four pages; LEFT / RIGHT (or A) turn them, B returns
 *
 *   UP / DOWN choose, A opens / arms / confirms, B (tap) back to the menu
 */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "gfx1.h"
#include "app_glue.h"
#include "storage.h"
#include "demos.h"
#include "proj.h"

enum { M_NEW, M_LOAD, M_SAVE, M_CODE, M_IMPDS, M_IMPGB, M_ABOUT, M_COUNT };
static const char *const menu_names[M_COUNT] = { "NEW", "LOAD", "SAVE", "CODE", "IMPORT DS", "IMPORT GB", "ABOUT" };
static uint8_t page = 0xFF, msel, isel, armed = 0xFF, erase_armed, about_pg, itop;
static char names[16][PROJECT_NAME_LEN + 1];
static uint8_t status[16];

#define N_ABOUT 4
static const char *const about_title[N_ABOUT] = { "ETCH CRANK", "GENERATIVE MUSIC", "SYNTH CENTRIC", "CREDITS" };
static const char *const about_text[N_ABOUT] = {
    "A generative acid machine by Synth Centric, for Playdate. Version " ETCH_CRANK_VERSION ".\n"
    "Its composer, CORROSION, writes the music and keeps rewriting it. An acid bass voice, drum lanes and an effects chain play it. The crank is the filter.",
    "Generative music is made by rules and chance. CORROSION composes from a seed and keeps rewriting, so every listen is different.\n"
    "A seed code always makes the same groove: share it to share the song.",
    "Dan Schwebel, known as Synth Centric, is an Australian beat maker, music producer, writer and educator.\n"
    "ETCH CRANK is a Synth Centric release.",
    "Code and CORROSION: MIT licence. Music, artwork, names: (c) Synth Centric.\n"
    "Sound engine from ETCH DS. Every sound is synthesised: no samples. Built with the Playdate SDK. Playdate is a trademark of Panic Inc.",
};

static int items(void)
{
    switch (page) {
    case M_LOAD: return DEMO_COUNT + 1 + 16;
    case M_SAVE: case M_IMPDS: return 16;
    case M_IMPGB: return GB_SLOTS;
    default: return 0;
    }
}

static void scan(uint8_t which, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        names[i][0] = 0;
        status[i] = proj_slot_status(which, (uint8_t)i, names[i]);
        names[i][PROJECT_NAME_LEN] = 0;
    }
}

static void open_page(uint8_t p)
{
    page = p;
    isel = 0;
    itop = 0;
    armed = 0xFF;
    erase_armed = 0;
    about_pg = 0;
    if (p == M_LOAD || p == M_SAVE) scan(0, 16);
    if (p == M_IMPDS) scan(1, 16);
    if (p == M_IMPGB) scan(2, GB_SLOTS);
}

void gen_focus_code(void);

static void file_event(const ui_ev_t *e)
{
    if (page == 0xFF) {                              /* the menu */
        if (e->nav_y) msel = (uint8_t)((msel + e->nav_y + M_COUNT) % M_COUNT);
        if (e->a_tap) {
            if (msel == M_NEW) {
                if (armed == M_NEW) { armed = 0xFF; proj_new(); }
                else { armed = M_NEW; ui_message("A AGAIN: A NEW, EMPTY PROJECT"); }
            } else if (msel == M_CODE) {
                ui_goto(SCR_GEN);
                gen_focus_code();
            } else open_page(msel);
        } else if (e->nav_y) armed = 0xFF;
        return;
    }
    if (e->b_tap) { page = 0xFF; armed = 0xFF; return; }
    if (page == M_ABOUT) {
        if (e->nav_x || e->a_tap) about_pg = (uint8_t)((about_pg + (e->nav_x ? e->nav_x : 1) + N_ABOUT) % N_ABOUT);
        return;
    }
    if (e->nav_y) {
        int n = items();
        isel = (uint8_t)((isel + e->nav_y + n) % n);
        armed = 0xFF;
        erase_armed = 0;
    }
    if (e->del && page == M_SAVE) { erase_armed = 1; ui_message("A ERASES SLOT %02u", isel + 1); return; }
    if (!e->a_tap) return;
    switch (page) {
    case M_LOAD:
        if (armed != isel) { armed = isel; ui_message("A AGAIN LOADS IT"); return; }
        if (isel < DEMO_COUNT) proj_demo(isel);
        else if (isel == DEMO_COUNT) proj_boot_song();
        else proj_load((uint8_t)(isel - DEMO_COUNT - 1));
        page = 0xFF;
        break;
    case M_SAVE:
        if (erase_armed) { proj_erase(isel); erase_armed = 0; scan(0, 16); return; }
        if (status[isel] == SLOT_OK && armed != isel) { armed = isel; ui_message("A AGAIN REPLACES SLOT %02u", isel + 1); return; }
        proj_save(isel);
        armed = 0xFF;
        scan(0, 16);
        break;
    case M_IMPDS:
    case M_IMPGB:
        if (armed != isel) { armed = isel; ui_message("A AGAIN IMPORTS IT"); return; }
        if (page == M_IMPDS) proj_import_ds(isel); else proj_import_gb(isel);
        page = 0xFF;
        break;
    }
}

static void item_text(int i, char *b)
{
    if (page == M_LOAD && i < DEMO_COUNT) { sprintf(b, "D%d  %s", i + 1, demo_names[i]); return; }
    if (page == M_LOAD && i == DEMO_COUNT) { sprintf(b, "    CORRODE  (FROM ITS CODE)"); return; }
    if (page == M_LOAD) i -= DEMO_COUNT + 1;
    if (status[i] == SLOT_OK) sprintf(b, "S%02d %s", i + 1, names[i]);
    else if (status[i] == SLOT_BAD) sprintf(b, "S%02d (DAMAGED)", i + 1);
    else sprintf(b, "S%02d -", i + 1);
}

static void file_draw(void)
{
    int i;
    char b[48];
    if (page == 0xFF) {
        for (i = 0; i < M_COUNT; i++) {
            int y = 72 + i * 20, sel = i == msel;
            if (sel) gfx_fill(6, y - 3, 160, 16, C_BLACK);
            gfx_text(12, y, F_MAIN, 2, sel ? C_WHITE : C_BLACK, menu_names[i]);
        }
        gfx_text(190, 72, F_SMALL, 1, C_BLACK, "PROJECT");
        memcpy(b, SH->proj.name, PROJECT_NAME_LEN); b[PROJECT_NAME_LEN] = 0;
        gfx_text(190, 80, F_MAIN, 2, C_BLACK, b);
        gfx_wrap(190, 104, 200, F_MAIN, 10, C_BLACK,
                 "SAVES GO TO THE GAME'S DATA FOLDER. ETCH DS SAVES LOAD AS THEY ARE: COPY ETCH_DS.SAV THERE. "
                 "ETCH GAME BOY SAVES IMPORT FROM ETCH_GB.SAV.");
        return;
    }
    if (page == M_ABOUT) {
        gfx_textf(8, 70, F_SMALL, 1, C_BLACK, "ABOUT   %d / %d", about_pg + 1, N_ABOUT);
        gfx_text(8, 80, F_MAIN, 2, C_BLACK, about_title[about_pg]);
        gfx_wrap(8, 102, 384, F_MAIN, 11, C_BLACK, about_text[about_pg]);
        for (i = 0; i < N_ABOUT; i++) gfx_fill(180 + i * 10, 214, 5, 5, i == about_pg ? C_BLACK : C_D50);
        return;
    }
    gfx_text(8, 68, F_SMALL, 1, C_BLACK, menu_names[page]);
    {
        int n = items(), vis = 13;
        if (isel < itop) itop = isel;
        if (isel >= itop + vis) itop = (uint8_t)(isel - vis + 1);
        for (i = 0; i < vis && itop + i < n; i++) {
            int k = itop + i, y = 78 + i * 11, sel = k == isel;
            item_text(k, b);
            if (sel) gfx_fill(6, y - 2, 240, 11, C_BLACK);
            gfx_text(10, y, F_MAIN, 1, sel ? C_WHITE : C_BLACK, b);
            if (sel && armed == k) gfx_text_r(240, y, F_MAIN, 1, C_WHITE, "A AGAIN");
        }
    }
}

static const char *file_hint(void)
{
    if (page == 0xFF) return "UP/DOWN CHOOSE   A OPEN";
    if (page == M_ABOUT) return "L/R OR A TURN THE PAGE   B BACK";
    if (page == M_SAVE) return "A SAVE   B+A THEN A ERASE   B BACK";
    return "A ARM, A AGAIN LOAD   B BACK";
}

static uint8_t file_can_ride(void) { return 0; }
static void file_enter(void) { page = 0xFF; armed = 0xFF; }

const screen_t scr_file = { "FILE", file_event, file_draw, file_can_ride, file_hint, file_enter };
