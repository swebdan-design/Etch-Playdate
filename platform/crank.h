/*
 * ETCH CRANK -- by Synth Centric
 * The crank is the 303's filter cutoff.
 *
 * Two halves, talking only through the pump's command ring:
 *
 * update loop (crank.c, at frame rate)
 *   - reads the crank's CHANGE in degrees (relative: picking the crank up
 *     never makes the filter jump) and accumulates it into the live value
 *   - 540 degrees from fully closed to fully open, with hard stops at both
 *     ends (no wrap; turning back moves at once).  The value is ACID's
 *     CUTOFF, whose 128 steps are linear in pitch (0.656 semitone each), so
 *     equal turns are equal musical intervals: the mapping is in octaves
 *   - held B: the crank turns resonance instead
 *   - docking: cutoff and resonance glide back to the stored values over one
 *     beat (linear in octaves); held B while docking keeps the live values as
 *     the new stored values instead
 *   - the live value is the stored value plus the crank's offset.  Only the
 *     offset is the crank's: CORROSION's living-song drift (evo_cut_ofs)
 *     stays where ETCH DS puts it, on top, and never touches the offset
 *   - sends the live values as targets (PCMD_CUTOFF, PCMD_RES, Q8)
 *
 * audio callback (crank_audio.c, at ACID's control rate, 1.02 kHz)
 *   - glides toward the latest target, one pole, tau 4 ms, per control tick
 *   - writes ACID's cutoff / resonance for the tick, then runs the real
 *     acid_render, which interpolates the ladder coefficient per sample
 *     across the tick: the filter moves every sample with no steps
 *   - linked in with -Wl,--wrap=acid_render so engine/ stays ETCH DS's
 *     files.  Until the crank is first used it writes nothing at all, and
 *     docked at rest it writes exactly the stored values: the output is
 *     ETCH DS's, bit for bit (the consistency test proves it)
 */
#ifndef ETCH_CRANK_CRANK_H
#define ETCH_CRANK_CRANK_H
#include <stdint.h>

#define CRANK_TRAVEL_DEG  540       /* fully closed -> fully open           */
#define CRANK_MAX_Q8      (127 << 8)

typedef struct {
    int32_t  cut_q8, res_q8;        /* live values (stored + offset), Q8    */
    uint8_t  stored_cut, stored_res;/* the stored values (0..127)          */
    uint8_t  docked, b_held;        /* last frame's inputs                  */
    uint8_t  returning;             /* gliding home after a dock            */
    uint32_t ret_t0_ms, ret_len_ms;
    int32_t  ret_cut0, ret_res0;    /* where the glide home started         */
    int32_t  sent_cut, sent_res;    /* last targets sent                    */
    uint8_t  engaged;               /* the crank has taken the filter       */
} crank_ui_t;
extern crank_ui_t crank_ui;

/* update loop: once at boot, with ACID's stored cutoff and resonance */
void crank_ui_init(uint8_t stored_cut, uint8_t stored_res, uint8_t docked);
/* update loop: once per frame.  delta_q8 = crank change in 1/256 degree,
 * now_ms = a millisecond clock, bpm = the song's tempo (for the glide home) */
void crank_ui_frame(int32_t delta_q8, uint8_t docked, uint8_t b_held,
                    uint32_t now_ms, uint8_t bpm);
/* the offset the crank is playing on top of the stored values, Q8 */
static inline int32_t crank_cut_offset_q8(void)
{ return crank_ui.cut_q8 - ((int32_t)crank_ui.stored_cut << 8); }

/* audio callback: a PCMD_* from the command ring */
void crank_command(uint32_t cmd);
void crank_audio_reset(void);         /* from pump_init                    */
/* audio callback: the value ACID is using now (Q8), for the meter */
extern volatile int32_t crank_cut_now_q8, crank_res_now_q8;

#endif
