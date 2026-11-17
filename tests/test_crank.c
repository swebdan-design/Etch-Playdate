/*
 * ETCH CRANK -- by Synth Centric
 * The crank as the 303's cutoff: host checks of both halves, through the real
 * pump, ETCH DS's engine and the link-time wrap.
 *
 *   gestures   relative (no jump on pick-up), 540 degrees fully closed to
 *              fully open exactly, hard stops at both ends with an immediate
 *              return, B + crank = resonance, docking glides home over one
 *              beat (linear in octaves), B + dock keeps the live values,
 *              picking the crank up mid-glide holds where it is
 *   audio      the glide reaches the target in a few milliseconds and moves
 *              ACID's ladder every control tick (which ACID interpolates per
 *              sample) with no step larger than the tick's share; docked at
 *              rest ACID gets exactly the stored values
 *   sound      the ACID stem gets brighter as the crank opens; CORROSION's
 *              living-song drift never touches the crank's offset
 *   listening  build/host/crank_sweep.wav: the boot song, crank swept closed
 *              -> open -> closed over 8 bars, then B + crank on resonance
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ipc.h"
#include "audio.h"
#include "app_glue.h"
#include "corrosion.h"
#include "pump.h"
#include "crank.h"

uint16_t hw_noise(void) { return 0x1234; }

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
                           printf(__VA_ARGS__); printf("\n"); } } while (0)

static void boot(void)
{
    audio_engine_init();
    foundry_build();
    shared_init();
    pump_init(SH);
    crank_ui_init(acid_p.cutoff, acid_p.res, 1);
}

static uint32_t now_ms;
static void frame(int32_t deg_q8, uint8_t docked, uint8_t b)
{
    crank_ui_frame(deg_q8, docked, b, now_ms, SH->proj.synth.bpm);
    now_ms += 20;
}

static void gestures(void)
{
    int i;
    int32_t c0;
    printf("  stored: cutoff %u, resonance %u (CORRODE's, via ETCH DS's demo_import_sound)\n",
           crank_ui.stored_cut, crank_ui.stored_res);
    c0 = crank_ui.cut_q8;
    frame(0, 0, 0);                          /* pick it up: nothing moves */
    CHECK(crank_ui.cut_q8 == c0, "undocking moved the cutoff");
    for (i = 0; i < 40; i++) frame(-25 * 256, 0, 0);   /* -1000 deg: past the closed stop */
    CHECK(crank_ui.cut_q8 == 0, "closed hard stop: %d", crank_ui.cut_q8);
    frame(5 * 256, 0, 0);                    /* turning back moves at once */
    CHECK(crank_ui.cut_q8 > 0, "no dead zone after the closed stop");
    frame(-5 * 256, 0, 0);
    CHECK(crank_ui.cut_q8 == 0, "back to closed");
    /* 540 degrees in uneven pieces: exactly closed -> open */
    {
        static const int piece[] = { 7, 13, 1, 29, 3, 11 };
        int32_t total = 0, k = 0;
        while (total + piece[k % 6] * 256 <= 540 * 256) { frame(piece[k % 6] * 256, 0, 0); total += piece[k % 6] * 256; k++; }
        frame(540 * 256 - total, 0, 0);
        CHECK(crank_ui.cut_q8 == CRANK_MAX_Q8, "540 degrees should open fully: %d of %d", crank_ui.cut_q8, CRANK_MAX_Q8);
        printf("  540 degrees in %d uneven turns: cutoff 0 -> %d.%02d (of 127)\n", (int)k + 1,
               crank_ui.cut_q8 >> 8, (crank_ui.cut_q8 & 255) * 100 / 256);
    }
    for (i = 0; i < 10; i++) frame(30 * 256, 0, 0);    /* past the open stop */
    CHECK(crank_ui.cut_q8 == CRANK_MAX_Q8, "open hard stop");
    frame(-(540 * 256) / 127, 0, 0);         /* one step's worth back */
    CHECK(abs(crank_ui.cut_q8 - (CRANK_MAX_Q8 - 256)) <= 1, "one step back from the open stop: %d", crank_ui.cut_q8);
    printf("  travel: 540 deg = 127 CUTOFF steps = %.1f semitones (%.2f octaves), %.2f deg per step\n",
           127 * 42 / 64.0, 127 * 42 / 64.0 / 12, 540 / 127.0);

    /* B + crank: resonance, cutoff untouched */
    {
        int32_t c = crank_ui.cut_q8, r = crank_ui.res_q8;
        frame(-90 * 256, 0, 1);
        CHECK(crank_ui.cut_q8 == c, "B + crank moved the cutoff");
        CHECK(crank_ui.res_q8 < r, "B + crank did not lower the resonance");
        printf("  B + crank -90 deg: resonance %d -> %d, cutoff unchanged\n", r >> 8, crank_ui.res_q8 >> 8);
    }

    /* dock: glide home over one beat, linear */
    {
        uint32_t beat = 60000u / SH->proj.synth.bpm, t0;
        int32_t from = crank_ui.cut_q8, home = (int32_t)crank_ui.stored_cut << 8, mid;
        frame(0, 1, 0);
        t0 = now_ms - 20;
        while (now_ms - t0 < beat / 2) frame(0, 1, 0);
        mid = crank_ui.cut_q8;
        CHECK(abs(mid - (from + home) / 2) < 256 * 6, "half a beat should be half way: %d (from %d to %d)", mid, from, home);
        while (now_ms - t0 <= beat + 20) frame(0, 1, 0);
        CHECK(crank_ui.cut_q8 == home && crank_ui.res_q8 == ((int32_t)crank_ui.stored_res << 8),
              "docked: should be home after a beat (%d %d)", crank_ui.cut_q8, crank_ui.res_q8);
        printf("  dock: home in one beat (%u ms at %u BPM), half way at half a beat (%d.%d)\n",
               beat, SH->proj.synth.bpm, mid >> 8, (mid & 255) * 10 / 256);
    }

    /* picked up mid-glide: holds where it is */
    {
        int32_t v;
        frame(0, 0, 0);
        frame(200 * 256, 0, 0);
        frame(0, 1, 0);
        frame(0, 1, 0);
        frame(0, 0, 0);                      /* picked up again */
        v = crank_ui.cut_q8;
        frame(0, 0, 0); frame(0, 0, 0);
        CHECK(crank_ui.cut_q8 == v, "picked up mid-glide: should hold");
        CHECK(v > ((int32_t)crank_ui.stored_cut << 8), "mid-glide value");
    }

    /* B + dock: keep */
    {
        uint8_t s0 = crank_ui.stored_cut;
        int32_t live = crank_ui.cut_q8;
        frame(0, 1, 1);
        CHECK(crank_ui.stored_cut == (uint8_t)((live + 128) >> 8) && crank_ui.stored_cut != s0,
              "B + dock should keep the live value (%u, live %d, was %u)", crank_ui.stored_cut, live, s0);
        frame(0, 1, 0); frame(0, 1, 0);
        CHECK(crank_ui.cut_q8 == ((int32_t)crank_ui.stored_cut << 8) && !crank_ui.returning, "B + dock: no glide");
        printf("  B + dock: stored cutoff %u -> %u\n", s0, crank_ui.stored_cut);
    }
}

/* ---- the audio half ------------------------------------------------------------- */
static void audio_half(void)
{
    int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK];
    int b, reached = -1, monotonic = 1;
    uint8_t last;
    boot();
    app_send(CMD(CMD_START, 0x4242));
    for (b = 0; b < 200; b++) pump_block(l, r);
    CHECK(acid_p.cutoff == crank_ui.stored_cut, "untouched before the crank moves");
    /* a big turn in one frame: 30 -> ~100 */
    frame(0, 0, 0);
    frame(300 * 256, 0, 0);
    last = acid_p.cutoff;
    for (b = 0; b < 40; b++) {
        pump_block(l, r);                    /* 4 control ticks */
        if (acid_p.cutoff < last) monotonic = 0;
        last = acid_p.cutoff;
        if (reached < 0 && acid_p.cutoff == (uint8_t)((crank_ui.cut_q8 + 128) >> 8)) reached = b;
    }
    CHECK(monotonic, "the glide should rise monotonically");
    CHECK(reached >= 0 && reached <= 8, "target not reached within 8 blocks (%d)", reached);
    printf("  glide: a %d-step jump reaches its target in %d blocks (%.1f ms), one-pole tau 4 ms per control tick\n",
           ((crank_ui.cut_q8 + 128) >> 8) - crank_ui.stored_cut, reached + 1, (reached + 1) * 128 / 32.7285);
    /* docked at rest: ACID gets exactly the stored values */
    frame(0, 1, 0);
    for (b = 0; b < 60; b++) { frame(0, 1, 0); pump_block(l, r); }
    for (b = 0; b < 100; b++) pump_block(l, r);
    CHECK(acid_p.cutoff == crank_ui.stored_cut && acid_p.res == crank_ui.stored_res,
          "docked at rest: ACID should have the stored values (%u/%u vs %u/%u)",
          acid_p.cutoff, acid_p.res, crank_ui.stored_cut, crank_ui.stored_res);
}

/* ---- sound: brightness by crank position ------------------------------------------ */
static double centroid_of_stem(int target_cut)
{
    /* ACID only (drums muted), 2 bars from bar 2 */
    static int16_t buf[2 * 4 * 16 * 128 * 4];
    int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK];
    uint32_t b, n = 0, k;
    double num = 0, den = 0;
    boot();
    app_send(CMD(CMD_MUTES, 0x1E));          /* lanes muted, bass plays */
    app_send(CMD(CMD_START, 0x4242));
    frame(0, 0, 0);
    frame((target_cut - crank_ui.stored_cut) * 540 * 256 / 127, 0, 0);
    for (b = 0; b < 500; b++) pump_block(l, r);
    printf("    (stem at crank %d: ACID cutoff %u, resonance %u)\n", target_cut, acid_p.cutoff, acid_p.res);
    for (b = 0; b < 1024 && n + AUDIO_BLOCK <= sizeof buf / 2; b++) {
        pump_block(l, r);
        memcpy(buf + n, l, sizeof l);
        n += AUDIO_BLOCK;
    }
    /* spectral centroid via a plain DFT on 4096-sample frames (host only) */
    for (k = 0; k + 4096 <= n; k += 4096) {
        int f;
        for (f = 1; f < 512; f++) {            /* up to ~4 kHz in 8 Hz bins */
            double re = 0, im = 0, hz = f * 32728.5 / 4096, m;
            int i;
            for (i = 0; i < 4096; i++) {
                double w = 0.5 - 0.5 * cos(2 * M_PI * i / 4096);
                re += buf[k + i] * w * cos(2 * M_PI * f * i / 4096);
                im -= buf[k + i] * w * sin(2 * M_PI * f * i / 4096);
            }
            m = sqrt(re * re + im * im);
            num += m * hz;
            den += m;
        }
    }
    return den > 0 ? num / den : 0;
}

static void sound(void)
{
    double c_closed = centroid_of_stem(10), c_home = centroid_of_stem(30), c_open = centroid_of_stem(100);
    printf("  ACID stem spectral centroid (0-4 kHz): crank at 10 -> %.0f Hz, 30 (stored) -> %.0f Hz, 100 -> %.0f Hz\n",
           c_closed, c_home, c_open);
    CHECK(c_closed < c_home && c_home < c_open, "the stem should brighten as the crank opens");
}

/* ---- the living song never touches the offset ------------------------------------- */
static void drift_vs_offset(void)
{
    int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK];
    int32_t ofs;
    uint32_t b, commits;
    boot();
    evo_seed(0x7777);
    app_send(CMD(CMD_START, 0x7777));
    frame(0, 0, 0);
    frame(85 * 256, 0, 0);                   /* +20 steps */
    ofs = crank_cut_offset_q8();
    for (b = 0; b < 8000; b++) {             /* ~16 bars of DRIFT */
        if ((b & 3) == 0) { frame(0, 0, 0); evo_frame(); }
        pump_block(l, r);
    }
    commits = SH->evo_commits;
    CHECK(commits >= 3, "DRIFT should have evolved (%u)", commits);
    CHECK(crank_cut_offset_q8() == ofs, "the crank's offset changed under evolution");
    printf("  %u DRIFT evolutions, living-song cutoff drift now %+d: the crank's offset stayed %+d.%02d steps\n",
           commits, evo_cut_ofs, ofs >> 8, (ofs & 255) * 100 / 256);
}

/* ---- listening: a sweep at 44.1 kHz ------------------------------------------------- */
static void sweep_wav(void)
{
    FILE *f = fopen("crank_sweep.wav", "wb");
    uint32_t frames = 0, k, bars = 10;
    uint32_t total = (uint32_t)(bars * 4 * 60.0 / 124 * 44100);
    int16_t l[441], r[441];
    if (!f) return;
    boot();
    app_send(CMD(CMD_START, 0x4242));
    fseek(f, 44, SEEK_SET);
    frame(0, 0, 0);
    while (frames < total) {
        double pos = (double)frames / total;  /* 0..1 over 10 bars */
        int32_t deg = 0;
        uint8_t b = 0;
        if (pos < 0.4) deg = (int32_t)(540.0 * 256 / (0.4 * 100));          /* open over 4 bars */
        else if (pos < 0.8) deg = -(int32_t)(540.0 * 256 / (0.4 * 100));   /* close over 4 bars */
        else { b = 1; deg = (int32_t)(270.0 * 256 / (0.2 * 100)); }          /* B: resonance */
        if (pos >= 0.4 && pos < 0.42) b = 0;
        frame(deg, 0, b);
        pump_pull(l, r, 441);
        for (k = 0; k < 441; k++) { int16_t s[2] = { l[k], r[k] }; fwrite(s, 4, 1, f); }
        frames += 441;
    }
    {
        uint32_t data = frames * 4, v;
        fseek(f, 0, SEEK_SET);
        fwrite("RIFF", 4, 1, f); v = 36 + data; fwrite(&v, 4, 1, f);
        fwrite("WAVEfmt ", 8, 1, f); v = 16; fwrite(&v, 4, 1, f);
        { uint16_t t[2] = { 1, 2 }; fwrite(t, 4, 1, f); }
        v = 44100; fwrite(&v, 4, 1, f); v = 44100 * 4; fwrite(&v, 4, 1, f);
        { uint16_t t[2] = { 4, 16 }; fwrite(t, 4, 1, f); }
        fwrite("data", 4, 1, f); fwrite(&data, 4, 1, f);
    }
    fclose(f);
    printf("  listen: build/host/crank_sweep.wav (%.1f s at 44.1 kHz: open over 4 bars, close over 4, then B + crank)\n",
           frames / 44100.0);
}

int main(void)
{
    boot();
    gestures();
    audio_half();
    sound();
    drift_vs_offset();
    sweep_wav();
    printf("test_crank: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
