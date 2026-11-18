/*
 * ETCH CRANK -- by Synth Centric
 * The interface: ETCH's six screens in ETCH's order,
 *
 *     SYNTH  <-  TRACK  <-  LIVE  ->  GEN  ->  SONG  ->  FILE      (LIVE is home)
 *
 * under an always-on status band, with ETCH's button grammar.  The Playdate
 * has no SELECT or START, so held B takes SELECT's role (docs/CONTROLS.md):
 *
 *   B + LEFT / RIGHT   previous / next screen (wraps)
 *   B + DOWN           play / stop                      (ETCH: START)
 *   B + UP             arm crank recording
 *   B + A              delete / back to default         (ETCH grammar)
 *   B tap              LIVE: FILL on / off; elsewhere back / cancel
 *   D-pad              move;  A tap: place / do;  A held + D-pad: change
 *   crank              cutoff; B + crank: resonance; A held on a value +
 *                      crank: rides that value; dock: home over a beat
 *
 * The framework (ui.c) turns raw buttons into these meanings once, so every
 * screen sees the same grammar.  Screens draw into the 1-bit buffer
 * (gfx1.h); a region is redrawn only when what it shows changed.
 */
#ifndef ETCH_CRANK_UI_H
#define ETCH_CRANK_UI_H
#include <stdint.h>

/* buttons: the Playdate SDK's bit values */
#define BTN_LEFT  0x01
#define BTN_RIGHT 0x02
#define BTN_UP    0x04
#define BTN_DOWN  0x08
#define BTN_B     0x10
#define BTN_A     0x20
#define BTN_DIRS  0x0F

enum { SCR_SYNTH, SCR_TRACK, SCR_LIVE, SCR_GEN, SCR_SONG, SCR_FILE, SCR_COUNT };

/* layout */
#define STATUS_H   64          /* status band, always on                    */
#define BODY_Y     66
#define BODY_H     161         /* 66 .. 226                                 */
#define HINT_Y     229

typedef struct {
    uint8_t  held, pushed, released;
    int32_t  crank_q8;          /* crank change this frame, 1/256 degree     */
    uint8_t  docked;
    uint32_t now_ms;
} ui_in_t;

/* what a screen receives: the grammar, already resolved */
typedef struct {
    int8_t  nav_x, nav_y;       /* plain D-pad (with key repeat)              */
    int8_t  adj_x, adj_y;       /* A held + D-pad (with key repeat)           */
    uint8_t a_tap;              /* A pressed and released, nothing in between */
    uint8_t del;                /* B + A                                      */
    uint8_t b_tap;              /* B pressed and released alone               */
    int32_t ride_q8;            /* A held + crank, on a value that can ride   */
} ui_ev_t;

typedef struct {
    const char *name;
    void (*event)(const ui_ev_t *e);
    void (*draw)(void);         /* the body, BODY_Y .. BODY_Y + BODY_H        */
    uint8_t (*can_ride)(void);  /* 1: the cursor is on a value the crank rides */
    const char *(*hint)(void);  /* one line saying what the buttons do here  */
    void (*enter)(void);
} screen_t;

extern const screen_t scr_synth, scr_track, scr_live, scr_gen, scr_song, scr_file;

void ui_init(void);
void ui_frame(const ui_in_t *in);       /* input + draw; the update loop */
void ui_goto(int screen);
void ui_message(const char *fmt, ...);
void ui_redraw(void);                    /* the body changed */
uint8_t ui_screen(void);
extern uint8_t ui_fill_on;
extern uint32_t ui_now_ms;

/* ---- a list of rows (SYNTH, GEN, SONG ...): caption, value, bar -------------
 * UP / DOWN choose a row; LEFT / RIGHT or A + LEFT / RIGHT step it; A + UP /
 * DOWN step it coarse; B + A resets it; A held + crank rides it; A tap runs
 * the row's action if it has one. */
typedef struct {
    const char *caption;
    int  (*get)(void);
    void (*set)(int v);
    int16_t min, max, coarse, dflt;
    void (*fmt)(int v, char *out);       /* NULL: the number                  */
    void (*action)(void);                /* A tap; NULL: none                 */
} row_t;

typedef struct {
    const row_t *rows;
    uint8_t n, sel, top;
    int32_t ride_acc;
} list_t;

void list_event(list_t *l, const ui_ev_t *e);
void list_draw(list_t *l, int x, int y, int w, int row_h, int visible);
uint8_t list_can_ride(const list_t *l);

/* small helpers shared by the screens */
const char *note_name(uint8_t note);    /* "C#2" */
void ui_bar(int x, int y, int w, int v, int lo, int hi, int selected);

#endif
