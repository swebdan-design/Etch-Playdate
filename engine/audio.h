/*
 * ETCH DS -- by Synth Centric
 * ARM9 sound engine: ACID (303 voice), FOUNDRY (drums), FX chain, renderer.
 *
 * Fixed point throughout, and no division or floating point anywhere on the
 * audio path (the IPC interrupt that renders a block):
 *   - oscillator phase: 32 bits per cycle
 *   - ACID internal signal: Q14 (16384 = 1.0), the ladder is bounded by a
 *     cubic saturator so its states always stay inside +-1.0
 *   - mix bus: int32, Q15 scale (32767 = full scale)
 *   - gains Q15, coefficients Q16, pitch in 1/64 semitone (0 = C1)
 * The code is portable C so the host tests (tests/host) can render it too.
 */
#ifndef ETCH_DS_AUDIO_H
#define ETCH_DS_AUDIO_H

#include <stdint.h>
#include "ipc.h"

#ifdef __NDS__
#include <nds/ndstypes.h>
#define AUDIO_CODE ITCM_CODE
#define AUDIO_DATA DTCM_DATA
#define AUDIO_BSS  DTCM_BSS
#else
#define AUDIO_CODE
#define AUDIO_DATA
#define AUDIO_BSS
#endif

#define CTRL_LEN  SEQ_SUB            /* control tick = 32 samples (1.02 kHz) */
#define Q14_ONE   16384

/* ---- ACID ----------------------------------------------------------------- */
enum { DRV_OFF, DRV_SOFT, DRV_HARD, DRV_FOLD, DRV_COUNT };
enum { CUT_303, CUT_KEY };          /* filter key tracking: off (303) / ETCH  */

typedef struct {
    uint8_t wave;       /* 0..3 SAW SQUARE HOLLOW BITE                        */
    uint8_t cutoff;     /* 0..127                                             */
    uint8_t res;        /* 0..127                                             */
    uint8_t envmod;     /* 0..127                                             */
    uint8_t decay;      /* 0..127   30 ms .. 4.5 s                            */
    uint8_t accent;     /* 0..127                                             */
    uint8_t slide;      /* 0..127   4 ms .. 250 ms, constant time             */
    uint8_t drive;      /* DRV_*                                              */
    uint8_t drive_amt;  /* 0..127                                             */
    uint8_t keytrack;   /* CUT_303 / CUT_KEY                                  */
    int8_t  tune;       /* semitones                                          */
    uint8_t level;      /* 0..127                                             */
} acid_params_t;

typedef struct {
    uint32_t phase, inc;
    int32_t  pitch, target;
    int32_t  pitch_q16, slide_step;  /* slide: pitch << 16 and its step per tick */
    uint16_t slide_left;
    uint8_t  held, active, stage, accented;
    int32_t  env;        /* filter envelope, Q16                              */
    uint32_t env_coef;
    int32_t  acc_env;    /* accent pulse, Q16                                 */
    int32_t  acc_cap;    /* accent capacitor, Q16: charges on repeated accents */
    int32_t  charge;     /* ETCH's per-step accent charge, 0..120             */
    int32_t  amp, peak;  /* VCA, Q15                                          */
    int32_t  g, k;       /* ladder coefficient (Q16) and feedback (Q12)       */
    int32_t  y1, y2, y3, y4;
    int32_t  dcx, dcy;   /* DC blocker after the drive                        */
    int32_t  dbg_cut;    /* last cutoff pitch (UI)                            */
} acid_t;

extern acid_params_t acid_p;
extern acid_t acid;
extern volatile int8_t evo_cut_ofs;        /* living-song timbre drift        */

void acid_init(void);
void acid_note(uint8_t note, uint8_t accent);
void acid_glide(uint8_t note, uint8_t accent);
void acid_gate_off(void);
void acid_step(void);
void acid_kill(void);
/* one control tick + CTRL_LEN samples, added (Q15) into out[] */
void acid_render(int32_t *out);

/* ---- FOUNDRY (drums) ---------------------------------------------------- */
#define KITS_PER_LANE 4
typedef struct {
    const int16_t *smp;
    uint32_t len;        /* samples                                         */
    uint32_t pos;        /* Q16                                             */
    uint32_t rate;       /* Q16, 65536 = as rendered                        */
    int32_t  gain;       /* Q15                                             */
    int32_t  fade;       /* > 0: choke ramp in progress (Q15 gain step)     */
    uint8_t  active;
} dvoice_t;

extern int8_t evo_tune_ofs[DS_LANES];
extern uint8_t foundry_tune[DS_LANES], foundry_decay[DS_LANES], foundry_level[DS_LANES];
/* a drum sound, synthesised from these and rendered to a sample */
enum { DT_KICK, DT_SNARE, DT_CLAP, DT_RIM, DT_HAT, DT_METALHAT, DT_BLIP, DT_RING, DT_CRUSH, DT_COUNT };
typedef struct {
    uint8_t  type;        /* DT_*                                                 */
    uint16_t f0, f1;      /* Hz: start / end (tones), or the noise colour         */
    uint16_t pitch_ms;    /* pitch sweep time (attack for hats)                   */
    uint16_t amp_ms;      /* decay to e^-6                                        */
    uint8_t  noise;       /* 0..255 noise mix / click / metal amount              */
    uint8_t  drive;       /* 0..255                                               */
    uint16_t len_ms;      /* sample length (foundry_set_len)                      */
} drum_def_t;
extern const drum_def_t foundry_presets[DS_LANES][KITS_PER_LANE];   /* ETCH's kits */
extern drum_def_t foundry_defs[DS_LANES][KITS_PER_LANE];            /* edited      */
extern const char * const foundry_model_names[DT_COUNT];
void foundry_build(void);              /* render every kit sound (boot, load) */
void foundry_rebuild(uint32_t lane, uint32_t kit);   /* after an edit (main loop) */
void foundry_set_len(drum_def_t *d);
const int16_t *foundry_sample(uint32_t lane, uint32_t kit, uint32_t *len);
void foundry_trigger(uint8_t lanes, uint8_t kits);
void foundry_kill(void);
void foundry_render(int32_t *l, int32_t *r, uint32_t n);
extern const char * const foundry_kit_names[DS_LANES][KITS_PER_LANE];

/* ---- FX chain --------------------------------------------------------------- */
typedef struct {
    uint8_t drive;      /* 0..127, 0 = off                                    */
    uint8_t crush_bits; /* 16 = off .. 4                                      */
    uint8_t crush_hold; /* 1 = off .. 16 samples                             */
    uint8_t dly_steps;  /* delay time in 16ths (1..8)                         */
    uint8_t dly_fb;     /* 0..127                                             */
    uint8_t dly_mix;    /* 0..127                                             */
    uint8_t dly_rot;    /* 0..127 degradation per repeat                      */
    uint8_t comb_note;  /* tuning, note number (0 = C1)                       */
    uint8_t comb_fb;    /* 0..127                                             */
    uint8_t comb_mix;   /* 0..127                                             */
    uint8_t limit;      /* 1 = on                                             */
    uint8_t bpm;        /* for the tempo sync                                 */
} fx_params_t;

extern fx_params_t fx_p;
void fx_init(void);
void fx_drive(int32_t *l, int32_t *r, uint32_t n);
void fx_crush(int32_t *l, int32_t *r, uint32_t n);
void fx_delay(int32_t *l, int32_t *r, uint32_t n);
void fx_comb(int32_t *l, int32_t *r, uint32_t n);
void fx_limit(int32_t *l, int32_t *r, int16_t *ol, int16_t *or_, uint32_t n);

/* ---- renderer -------------------------------------------------------------- */
enum { ST_EVENTS, ST_ACID, ST_DRUMS, ST_DRIVE, ST_CRUSH, ST_DELAY, ST_COMB,
       ST_LIMIT, ST_OUT, ST_COUNT };
typedef struct {
    uint32_t sum[ST_COUNT];   /* bus clocks in the current window             */
    uint32_t max_total;       /* worst block in the current window            */
    uint32_t blocks;
    /* last completed window, as percent x 100 of the block period */
    uint16_t avg_pct[ST_COUNT];
    uint16_t avg_total, max_total_pct;
    uint32_t windows;
} audio_stats_t;
extern volatile audio_stats_t astats;
extern const char * const stage_names[ST_COUNT];

/* live notes from the KEYS tab (main loop -> render interrupt): EV_NOTE,
 * EV_GLIDE, EV_GATE_OFF (while a key is held the sequencer's bass is muted);
 * EV_DRUMS (lanes, kits) auditions drums from the DRUM page. */
void live_note(uint8_t type, uint8_t note, uint8_t accent);

/* render one block from its events into out_l/out_r (int16) */
void audio_render_block(const evslot_t *evs, uint32_t blk, int16_t *out_l, int16_t *out_r);
void audio_engine_init(void);

#endif
