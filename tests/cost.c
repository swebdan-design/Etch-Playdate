/*
 * ETCH CRANK -- by Synth Centric
 * The workload for tools/arm_cost.sh: the Playdate path (pump, crank, ETCH
 * DS's engine, resampler) for a number of seconds at 44.1 kHz, as the audio
 * callback runs it, built for the Cortex-M7 and run under qemu-arm with
 * instruction tracing.
 *
 *   cost song SECONDS     the boot song in DRIFT, crank moving all the time
 *   cost stress SECONDS   ETCH DS's CPU test: ACID retriggered with an accent
 *                         on every 16th, all 6 drum lanes on every 16th, every
 *                         FX stage working (drive, crush, delay, comb,
 *                         limiter), crank moving
 *   cost full SECONDS     the song with every layer at its worst: a RUST
 *                         3-note stab on every step, METAL and NOISE on every
 *                         step, ratchets x4 on every drum hit; FX all working
 *
 * The update loop's work (CORROSION, the crank's gestures) runs too, between
 * callbacks, at 50 Hz; the cost script attributes instructions to functions,
 * so it is counted separately from the audio callback.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "audio.h"
#include "app_glue.h"
#include "corrosion.h"
#include "pump.h"
#include "crank.h"
#include "layers.h"

uint16_t hw_noise(void) { return 0x5A5A; }

int main(int argc, char **argv)
{
    int stress = argc > 1 && !strcmp(argv[1], "stress");
    int full = argc > 1 && !strcmp(argv[1], "full");
    static uint8_t ldmem[sizeof(layers_data_t)];
    double secs = argc > 2 ? atof(argv[2]) : 2.0;
    uint32_t total = (uint32_t)(secs * 44100), frames = 0, ms = 0, f = 0;
    int16_t l[256], r[256];

    audio_engine_init();
    foundry_build();
    shared_init();
    pump_init(SH);
    crank_ui_init(acid_p.cutoff, acid_p.res, 0);
    evo_seed(0xC0DE);
    app_send(CMD(CMD_START, 0xC0DE));
    layers_bind(ldmem, sizeof ldmem);
    layers_init();
    if (full) {
        int p, st;
        LD->manual = 1;
        LD->rust.mode = RM_PARA3;
        for (p = 0; p < NUM_PATTERNS; p++) {
            LD->pat[p].lock = 1;
            LD->pat[p].metal = LD->pat[p].noise = 0xFFFF;
            for (st = 0; st < 16; st++) { LD->pat[p].note[st] = 36; LD->pat[p].flags[st] = RF_ON | RF_CHORD; LD->pat[p].ratchet[st] = 0xFF; }
        }
        fx_p.drive = 50; fx_p.crush_bits = 10; fx_p.crush_hold = 3;
        fx_p.dly_mix = 60; fx_p.dly_fb = 80; fx_p.comb_mix = 40; fx_p.comb_fb = 90;
    }
    if (stress) {
        fx_p.drive = 50; fx_p.crush_bits = 10; fx_p.crush_hold = 3;
        fx_p.dly_mix = 60; fx_p.dly_fb = 80; fx_p.comb_mix = 40; fx_p.comb_fb = 90;
        app_send(CMD(CMD_STRESS, 1));
    }
    /* warm up 1 s (the trace window is marked by the script with the same run) */
    while (frames < total) {
        /* the update loop at 50 Hz: the crank sweeps back and forth */
        if (frames >= f * 882u) {
            int32_t d = ((f / 50) & 1) ? -6 * 256 : 6 * 256;   /* 300 deg/s */
            crank_ui_frame(d, 0, 0, ms, SH->proj.synth.bpm);
            layers_frame();
            evo_frame();
            app_fill_frame();
            f++;
            ms += 20;
        }
        pump_pull(l, r, 256);               /* the audio callback, 5.8 ms */
        frames += 256;
    }
    printf("%s: %.1f s, %u engine blocks, %u callbacks, peak cutoff %d\n", full ? "full" : stress ? "stress" : "song",
           frames / 44100.0, pump_blocks(), (unsigned)pstats.callbacks, crank_cut_now_q8 >> 8);
    return 0;
}
