/*
 * ETCH CRANK -- by Synth Centric
 * The ETCH DS layers: what ETCH DS's design adds around CORROSION, built here
 * so they can move into ETCH-DS unchanged (portable C, no Playdate calls).
 * ETCH DS's engine and CORROSION are not edited: the layers reach them only
 * through link-time wraps and ETCH DS's own API.
 *
 *   RUST       a second synth voice: two oscillators (ACID's band-limited
 *              SAW / SQUARE / HOLLOW / BITE), detune, hard sync, ring mod,
 *              mono or 3-note paraphonic through one resonant filter
 *   METAL, NOISE   FOUNDRY's lanes 4 and 5 (ETCH DS synthesises them; ETCH's
 *              patterns never write them), with their own 16-step parts
 *   ratchets   1-4 hits a step on ETCH's four drum lanes
 *   lengths    RUST, METAL and NOISE each run 1-16 steps (polymeter)
 *   the answering generator   writes RUST, METAL and NOISE as answers to
 *              CORROSION's bass and drums, with its own random stream seeded
 *              by the code's 4th group (the series' expansion seed): the same
 *              16-digit code gives the same layers everywhere, and a 12-digit
 *              code (no 4th group) leaves them silent, so ETCH's songs and
 *              codes sound exactly as on ETCH DS
 *   sound evolution   after each evolution the sound drifts around its anchor
 *              (bounded, per knob, with locks); off by default
 *
 * Timing: the layers step on ACID's per-16th call (acid_step, wrapped), which
 * ETCH DS's renderer makes on the exact control tick of each step, and render
 * inside FOUNDRY's per-chunk call (foundry_render, wrapped), so they are
 * sample-locked to ETCH's clock with no change to the sequencer.
 */
#ifndef ETCH_LAYERS_H
#define ETCH_LAYERS_H
#include <stdint.h>
#include "etch.h"
#include "audio.h"

/* RUST step flags */
#define RF_ON     0x01
#define RF_ACCENT 0x02
#define RF_CHORD  0x04            /* paraphonic: a 3-note stab              */
#define RF_TIE    0x08            /* hold into the next step                */

enum { RW_SAW, RW_SQUARE, RW_HOLLOW, RW_BITE };
enum { RM_MONO, RM_PARA3 };

typedef struct {
    uint8_t note[NUM_STEPS];      /* 0 = C1 ... 59                           */
    uint8_t flags[NUM_STEPS];
    uint16_t metal, noise;        /* 16-step hit masks                       */
    uint8_t ratchet[NUM_STEPS];   /* 2 bits per ETCH lane: hits-1 (0..3)     */
    uint8_t rust_len, metal_len, noise_len;
    uint8_t lock;                 /* 1: the answering generator leaves it   */
    uint32_t src_hash;            /* what it answered (CORROSION's material) */
} layer_pat_t;                    /* 56 bytes */

typedef struct {
    uint8_t wave_a, wave_b;
    uint8_t interval;             /* B: semitones above A (0..24)           */
    uint8_t detune;               /* 0..127 (up to a quarter tone)          */
    uint8_t sync, ring;           /* sync on/off; ring 0..127               */
    uint8_t cutoff, res, envmod, decay;
    uint8_t mode;                 /* RM_MONO / RM_PARA3                     */
    uint8_t level, gate;          /* level 0..127; gate 1..15 sixteenths of a step */
    uint8_t pad[3];
} rust_params_t;

typedef struct {
    layer_pat_t pat[NUM_PATTERNS];        /* 1792 bytes */
    rust_params_t rust;
    drum_def_t drums45[2];                /* METAL and NOISE designs (kit 0)  */
    uint8_t  evo_on;                      /* sound evolution                  */
    uint8_t  evo_lock;                    /* bit per knob: locked              */
    uint8_t  evo_anchor[8];
    uint8_t  enabled;                     /* the layers play                   */
    uint8_t  manual;                      /* written by hand: they play anyway */
} layers_data_t;

/* the knobs sound evolution may move */
enum { EK_ENVMOD, EK_DECAY, EK_ACCENT, EK_DRVAMT, EK_RUSTRES, EK_RUSTCUT, EK_DLYMIX, EK_COMBMIX, EK_COUNT };
extern const char *const evo_knob_names[EK_COUNT];

extern layers_data_t *LD;                 /* inside the project's sidecar      */
extern rust_params_t rust_p;              /* the voice's live parameters       */

void layers_bind(void *mem, uint32_t size);   /* where the data lives (the sidecar) */
void layers_init(void);
extern uint8_t *layers_knob[EK_COUNT];    /* the edited sound's knobs (platform)  */
extern uint8_t layers_has_ext;            /* the code has its 4th group (platform) */
extern uint16_t layers_ext_seed;          /* ... and its value                     */
uint8_t layers_stale(uint8_t pat);
void layers_frame(void);                  /* update loop                        */
void layers_project_loaded(void);         /* after a load / new project         */
void layers_generate(uint8_t pat);        /* the answering generator, one pattern */
void layers_answer_all(void);             /* every pattern with material        */
uint8_t layers_active(void);
void layers_evo_anchor(void);             /* the sound as it is now is home     */

/* audio callback: what FOUNDRY just triggered (from the trigger guard) */
void layers_on_trigger(uint8_t lanes);
/* audio callback: RUST into the bus (from the foundry_render wrap) */
void rust_init(void);
void rust_note(const uint8_t *notes, uint8_t n, uint8_t accent, uint8_t glide);
void rust_gate_off(void);
void rust_render(int32_t *L, int32_t *R, uint32_t n);
uint8_t rust_busy(void);

#endif
