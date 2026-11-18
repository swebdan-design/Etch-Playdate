/*
 * ETCH CRANK -- by Synth Centric
 * CPL: the Playdate-only layer.  Everything here sits around ETCH DS's engine
 * and CORROSION without changing them; with every feature off (the crank
 * docked, no lane, no scene morph, no mods, SPEAKER off, BITE off) the sound
 * and the composition are exactly ETCH DS's.
 *
 *   motion lanes   B + UP arms; from the next bar one pass of crank movement
 *                  is written to the pattern's cutoff lane (64 points a bar),
 *                  then plays back; further cranking rides on top.  CORRODE
 *                  shifts, thins and re-times a lane around its own anchor
 *                  (the recording), DRIFT nudges it and pulls it back.
 *   CRANK BITE     with the crank out, how far the filter is open becomes the
 *                  mutation amount CORROSION is handed (its own intensity
 *                  argument; never a change to its rules)
 *   SPEAKER        on the limiter: mono, a gentle low-mid lift.  AUTO turns it
 *                  on when headphones are unplugged
 *   TILT           the delay send (off by default)
 *   scenes         four snapshots of the sound; A recalls, A held + crank
 *                  morphs towards one
 *   mute groups    two groups of lanes toggled together
 *   mod matrix     four rows: source (CRANK TILT LFO ENV MIC) -> destination,
 *                  depth.  The crank still always drives cutoff
 *   the sidecar    all of the above per save slot, in etch_crank_ext.sav,
 *                  with ETCH's checksum-first, magic-last order; the project
 *                  itself stays in ETCH DS's own format (etch_crank.sav)
 */
#ifndef ETCH_CRANK_CPL_H
#define ETCH_CRANK_CPL_H
#include <stdint.h>
#include "audio.h"
#include "etch.h"

#define LANE_PTS      64            /* 16 steps x 4                        */
#define CPL_SCENES    4
#define CPL_MODS      4
#define CPL_MGROUPS   2

enum { SPK_AUTO, SPK_ON, SPK_OFF };
enum { MS_OFF, MS_CRANK, MS_TILT, MS_LFO, MS_ENV, MS_MIC, MS_COUNT };
enum { MD_ENVMOD, MD_DECAY, MD_ACCENT, MD_DRVAMT, MD_RES, MD_CUTOFF, MD_DLYMIX, MD_DLYFB,
       MD_COMBMIX, MD_CRUSH, MD_LEVEL, MD_COUNT };

typedef struct {
    uint8_t used;
    acid_params_t acid;
    fx_params_t fx;
    uint8_t mutes;
} scene_t;

typedef struct { uint8_t src, dst; int8_t depth; } modrow_t;

/* everything saved per project (the sidecar slot) */
typedef struct {
    char     ext[4];                       /* expansion seed: the 4th code group */
    uint8_t  has_ext;
    uint8_t  lfo_rate, lfo_shape, pad0;
    uint32_t lane_on;                      /* bit p: pattern p has a lane       */
    int16_t  lane[NUM_PATTERNS][LANE_PTS]; /* cutoff offsets, Q8 steps          */
    int16_t  anchor[NUM_PATTERNS][LANE_PTS];
    scene_t  scene[CPL_SCENES];
    uint8_t  mgroup[CPL_MGROUPS];          /* mute bits per group               */
    modrow_t mod[CPL_MODS];
    uint8_t  layers[6144];                 /* the DS layers (layers/)           */
} cpl_project_t;

typedef struct { uint8_t speaker, tilt, bite; } cpl_settings_t;

extern cpl_project_t cplp;
extern cpl_settings_t cpl_set;
/* the sound as edited (SYNTH, scenes); acid_p / fx_p = this + modulation */
extern acid_params_t base_acid;
extern fx_params_t base_fx;

void    cpl_init(void);
void    cpl_new_project(void);             /* empty sidecar data             */
void    cpl_sync_base(void);               /* base = the engine's values now  */
void    cpl_before_save(void);             /* acid_p / fx_p = base            */
void    cpl_frame(uint32_t now_ms, float tilt_x, int headphones);

/* motion lanes */
void    cpl_rec_arm(void);
uint8_t cpl_rec_state(void);               /* 0 idle, 1 armed, 2 recording   */
uint8_t cpl_motion_playing(void);
void    cpl_lane_clear(uint8_t pat);
void    cpl_lanes_evolve(uint8_t mode);    /* after a commit (CORRODE / DRIFT) */

/* the expansion seed (4th code group) */
const char *cpl_ext_code(void);            /* "" or four hex digits           */
uint16_t cpl_ext_seed(void);

uint8_t cpl_speaker_on(void);
uint8_t cpl_bite_active(void);

/* scenes */
void    cpl_scene_store(int i);
void    cpl_scene_recall(int i);
void    cpl_scene_clear(int i);
void    cpl_morph_begin(int i);
void    cpl_morph_by(int32_t delta_q8);    /* crank degrees, Q8              */
uint8_t cpl_morph_amount(void);            /* 0..127                          */

/* mute groups */
void    cpl_mgroup_toggle(int g);
uint8_t cpl_mgroup_of(uint8_t bit);        /* 0 none, 1, 2                    */
void    cpl_mgroup_assign(uint8_t bit, uint8_t g);

/* the sidecar */
uint8_t cpl_save_slot(uint8_t slot);
uint8_t cpl_load_slot(uint8_t slot);       /* 0: none saved (fresh data)       */
void    cpl_settings_save(void);
void    cpl_settings_load(void);
extern const char *cpl_ext_path, *cpl_settings_path;

extern const char *const mod_src_names[MS_COUNT];
extern const char *const mod_dst_names[MD_COUNT];
extern volatile uint8_t cpl_mic_level;     /* 0..127, from the mic callback   */
extern uint8_t cpl_mic_wanted;             /* a mod row uses the mic          */
extern uint8_t cpl_tilt_wanted;

#endif
