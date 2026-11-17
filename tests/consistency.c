/*
 * ETCH CRANK -- by Synth Centric
 * The sound consistency test: one ETCH demo song, from its seed code, with a
 * fixed dice seed and the crank docked, for 16 bars, through ETCH DS's engine
 * and through this port.  The two must be bit-identical.
 *
 * Built twice from this file:
 *
 *   consist_ds    ETCH DS, as ETCH DS runs it: a command reaches seq_command
 *                 directly between two blocks (the ARM7's FIFO interrupt),
 *                 then seq_render_block (the ARM7's block interrupt), then
 *                 audio_render_block (the ARM9's render interrupt).  Compiled
 *                 from an ETCH-DS checkout when DS=... is given to make,
 *                 otherwise from engine/ (checksum-identical to ETCH DS).
 *                 Nothing from platform/ is linked.
 *
 *   consist_port  ETCH CRANK, as the Playdate runs it: commands go through
 *                 the update loop -> callback ring (app_send in pump.c); the
 *                 "audio callback" is pump_pull at 44.1 kHz with uneven
 *                 lengths, which renders engine blocks on demand; the crank
 *                 is linked in (-Wl,--wrap=acid_render) and its update-loop
 *                 half runs every frame, docked; the update loop (50 Hz)
 *                 runs evo_frame and app_fill_frame.  The engine-rate blocks
 *                 are captured at the pump's tap, before the resampler.  The
 *                 44.1 kHz output is written too, for listening.
 *
 * The update loop's schedule.  CORROSION's commit lands in render time (at
 * step 16's gate point), but ETCH's sound drift after an evolution (a small
 * nudge of the cutoff and drum tuning, evolve.c) is applied by the update
 * loop when it next notices the commit, so WHEN it lands depends on the
 * update loop's phase against the audio: on ETCH DS that is the ARM9's main
 * loop (~60 Hz, free-running), on the Playdate the 50 Hz update.  So:
 *
 *   consist_port OUT.raw [WAV] aligned   (default) the update loop runs at the
 *       same engine-block boundaries as the ETCH DS side (every 4 blocks):
 *       must be bit-identical.  This is the consistency test.
 *   consist_port OUT.raw [WAV] 50hz      the update loop at 50 Hz between
 *       uneven callbacks, as on the device: the nudge lands up to a frame
 *       apart, the only expected difference; compare reports where.
 *
 * Both run the same script: the song (ETCH's demo 1, CORRODE, from
 * 0F1C-9009-5870, in DRIFT), START with dice seed 0xC0DE, hw_noise fixed (so
 * CORROSION's evolutions are reproducible), 16 bars at 124 BPM.  Each writes
 * its engine-rate output, interleaved L/R int16, and prints a hash.
 * compare.c reports the first difference: where, and what was playing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "seq_core.h"
#include "audio.h"
#include "app.h"
#include "corrosion.h"
#ifdef CONSIST_PORT
#include "pump.h"
#include "crank.h"
#include "app_glue.h"
#endif

#define BARS 16u
#define SEED 0xC0DEu

#ifndef CONSIST_PORT
shared_t *SH;
volatile uint16_t ui_entropy;
uint8_t app_card;
uint8_t app_next_bar_pattern(void) { return seq_next_bar_pattern(); }
void app_send(uint32_t cmd) { seq_command(cmd); }      /* the ARM7's FIFO interrupt */
#endif
uint16_t hw_noise(void) { return 0x5A5A; }              /* reproducible evolutions */

/* 16 bars at 124 BPM in engine samples, rounded up to whole blocks */
static uint32_t song_blocks(void)
{
    double secs = BARS * 4.0 * 60.0 / 124.0;
    return (uint32_t)(secs * 32728.498 / AUDIO_BLOCK) + 1;
}

static int16_t *out;
static uint32_t nblk, got;

#ifdef CONSIST_PORT
static void tap(const int16_t *l, const int16_t *r, uint32_t blk)
{
    uint32_t i;
    if (blk >= nblk) return;
    for (i = 0; i < AUDIO_BLOCK; i++) {
        out[(blk * AUDIO_BLOCK + i) * 2] = l[i];
        out[(blk * AUDIO_BLOCK + i) * 2 + 1] = r[i];
    }
    got = blk + 1;
}
#endif

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "consist.raw";
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    FILE *f;

    nblk = song_blocks();
    out = (int16_t *)calloc((size_t)nblk * AUDIO_BLOCK * 2, sizeof(int16_t));

#ifdef CONSIST_PORT
    {   /* the Playdate boot, as platform/main.c does it */
        static const uint32_t cb_len[] = { 256, 383, 512, 128, 441, 300 };
        const char *wav = argc > 2 && strcmp(argv[2], "-") ? argv[2] : NULL;
        int aligned = !(argc > 3 && !strcmp(argv[3], "50hz"));
        FILE *fw = wav ? fopen(wav, "wb") : NULL;
        uint32_t k = 0, out_frames = 0, next_frame = 0, ms = 0, framed = 0xFFFFFFFFu;
        audio_engine_init();
        foundry_build();
        shared_init();
        pump_init(SH);
        pump_tap = tap;
        crank_ui_init(acid_p.cutoff, acid_p.res, 1);
        ui_entropy = SEED;
        evo_seed(SEED);
        app_send(CMD(CMD_START, SEED));
        while (got < nblk) {
            int16_t l[512], r[512];
            uint32_t n, j;
            if (aligned) {
                /* the update loop before every 4th engine block, as the
                 * ETCH DS side; short callbacks render at most one block */
                n = 32;
                if ((pump_blocks() & 3) == 0 && pump_blocks() != framed) {
                    framed = pump_blocks();
                    crank_ui_frame(0, 1, 0, ms, SH->proj.synth.bpm);   /* docked */
                    evo_frame();
                    app_fill_frame();
                    ms += 16;
                }
            } else {
                /* the update loop: 50 frames a second, between uneven callbacks */
                n = cb_len[k++ % 6];
                while (out_frames >= next_frame) {
                    crank_ui_frame(0, 1, 0, ms, SH->proj.synth.bpm);   /* docked */
                    evo_frame();
                    app_fill_frame();
                    next_frame += 882;
                    ms += 20;
                }
            }
            pump_pull(l, r, n);                  /* the audio callback */
            if (fw) for (j = 0; j < n; j++) { int16_t s[2] = { l[j], r[j] }; fwrite(s, 4, 1, fw); }
            out_frames += n;
        }
        if (fw) fclose(fw);
    }
#else
    {
        static shared_t sh;
        uint32_t b;
        memset(&sh, 0, sizeof sh);
        SH = &sh;
        sh.queued = 0xFF;
        sh.evo_undo_pat = 0xFF;
        demo_load(&sh.proj);
        demo_import_sound(&sh.proj);
        audio_engine_init();
        foundry_build();
        seq_init(&sh);
        ui_entropy = SEED;
        evo_seed(SEED);
        app_send(CMD(CMD_START, SEED));
        for (b = 0; b < nblk; b++) {
            int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK];
            if ((b & 3) == 0) { evo_frame(); app_fill_frame(); }   /* the ~60 Hz main loop */
            seq_render_block(b);
            audio_render_block(&sh.ev[b & (AUDIO_NBLK - 1)], b, l, r);
            for (i = 0; i < AUDIO_BLOCK; i++) {
                out[(b * AUDIO_BLOCK + i) * 2] = l[i];
                out[(b * AUDIO_BLOCK + i) * 2 + 1] = r[i];
            }
        }
        got = nblk;
    }
#endif
    for (i = 0; i < nblk * AUDIO_BLOCK * 2; i++) {
        uint16_t v = (uint16_t)out[i];
        h = (h ^ (v & 0xFF)) * 1099511628211ull;
        h = (h ^ (v >> 8)) * 1099511628211ull;
    }
    f = fopen(path, "wb");
    if (!f || fwrite(out, (size_t)nblk * AUDIO_BLOCK * 4, 1, f) != 1) { printf("cannot write %s\n", path); return 1; }
    fclose(f);
    {   /* the end state: what was composed and how the sound drifted */
        uint64_t ph = 1469598103934665603ull;
        const uint8_t *pb = (const uint8_t *)SH->proj.pat;
        for (i = 0; i < sizeof SH->proj.pat; i++) ph = (ph ^ pb[i]) * 1099511628211ull;
#ifdef CONSIST_PORT
        printf("  port %-7s audio %016llx | %u evolutions, patterns %08x, drift cut %+d tune %+d %+d %+d %+d\n",
               argc > 3 && !strcmp(argv[3], "50hz") ? "50 Hz:" : "aligned:",
#else
        printf("  ETCH DS        audio %016llx | %u evolutions, patterns %08x, drift cut %+d tune %+d %+d %+d %+d\n",
#endif
               (unsigned long long)h, SH->evo_commits, (unsigned)(ph ^ (ph >> 32)), evo_cut_ofs,
               evo_tune_ofs[0], evo_tune_ofs[1], evo_tune_ofs[2], evo_tune_ofs[3]);
    }
    return 0;
}
