/*
 * ETCH CRANK -- by Synth Centric
 * Engine rate (ETCH DS's 32 728.498 Hz) -> Playdate output (44 100 Hz).
 *
 * A 16-tap polyphase windowed-sinc interpolator: 128 phases, Q15; the
 * outputs of the two neighbouring phases are interpolated linearly.  Two
 * taps per SMLAD on the Cortex-M7 (plain C on the host, bit-identical).
 * Integer multiply/shift/add only.  Every row's absolute sum is below 65536
 * (61 044 at most), so a 16-tap sum of int16 samples never leaves int32.  The step is 32.32 fixed point, accurate
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
