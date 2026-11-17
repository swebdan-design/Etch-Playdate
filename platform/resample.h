/*
 * ETCH CRANK -- by Synth Centric
 * Engine rate (ETCH DS's 32 728.498 Hz) -> Playdate output (44 100 Hz).
 *
 * A 16-tap polyphase windowed-sinc interpolator: 128 phases, the
 * coefficients interpolated linearly between neighbouring phases, Q15.
 * Integer multiply/shift/add only.  The step is 32.32 fixed point, accurate
 * to better than 2^-32 per sample, so the engine's tempo (which counts
 * engine samples) is exactly ETCH DS's in wall-clock time.
 */
#ifndef ETCH_CRANK_RESAMPLE_H
#define ETCH_CRANK_RESAMPLE_H
#include <stdint.h>

#define RS_TAPS    16
#define RS_PHASES  128
#define RS_PHASE_BITS 7

extern const int16_t rs_coef[RS_PHASES + 1][RS_TAPS];
extern const uint32_t rs_step_frac;

#endif
