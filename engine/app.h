/*
 * ETCH DS -- by Synth Centric
 * ARM9 application glue shared by main.c, evolve.c, demo.c and ui.c.
 */
#ifndef ETCH_DS_APP_H
#define ETCH_DS_APP_H
#include <stdint.h>
#include "ipc.h"

extern shared_t *SH;                 /* uncached view of the shared block */
extern volatile uint16_t ui_entropy; /* stirred by every button press      */

uint16_t hw_noise(void);             /* free-running timer low bits        */
void     app_send(uint32_t cmd);     /* command to the ARM7                */
uint8_t  app_next_bar_pattern(void);

/* evolve.c */
void     evo_seed(uint16_t entropy);
uint16_t evo_random(void);
uint8_t  evo_service(void);
uint8_t  evo_frame(void);
void     anchors_forget(uint8_t pat);
uint8_t  evo_mutate_now(uint8_t pat, pattern_t *out);

/* edit.c: every pattern change goes through the ARM7 */
void     app_set_bass(uint8_t step, uint8_t note, uint8_t flags);
void     app_toggle_drum(uint8_t lane, uint8_t step);
void     app_replace(const pattern_t *p, uint8_t undo);
void     app_replace_at(uint8_t pat, const pattern_t *p, uint8_t undo);
void     app_queue(uint8_t pat);
void     app_chain(uint8_t pos, uint8_t pat, uint8_t rep);
void     app_song(uint8_t on);
void     app_jump(uint8_t pos);
uint8_t  app_pattern_empty(uint8_t pat);
uint8_t  app_save_slot(uint8_t slot);
uint8_t  app_load_slot(uint8_t slot);
uint8_t  app_import_gb(uint8_t slot);
extern uint8_t app_card;             /* SD card ready */
void     app_generate(uint8_t what);
uint16_t app_regen(uint8_t what);
void     app_fill(void);
void     app_fill_latch(uint8_t on);
void     app_fill_frame(void);
void     app_evolve(void);
void     app_undo(void);
void     app_toggle_mute(uint8_t bit);
void     app_set_kit(uint8_t lane, uint8_t kit);   /* the playing pattern's kit */
void     app_audition(uint8_t lane, uint8_t kit);

/* main.c: the last audio the ARM9 rendered (for the scope) */
uint32_t audio_scope(int16_t *out, uint32_t n);

/* demo.c */
#define DEMO_CODE "0F1C90095870"      /* ETCH demo song 1, CORRODE: 0F1C-9009-5870 */
void demo_load(project_t *p);        /* portable: also used by the host tests */
void demo_import_sound(const project_t *p);

#define ETCH_DS_VERSION "0.3"

/* title.c: the opening screen; returns the entropy of the START press */
void     title_show(void);          /* draw it (first thing at boot) */
uint16_t title_run(void);

/* ui.c (bottom screen) and scr_top.c */
void ui_init(void);
void ui_frame(uint32_t keys_down, uint32_t keys_held);
void top_init(void);
void top_frame(void);
extern uint8_t ui_stress;
#endif
