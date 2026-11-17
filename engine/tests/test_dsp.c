/*
 * ETCH DS -- by Synth Centric
 * Host tests for the ARM9 sound engine (the same C the DS runs):
 *   ACID stability over every wave x drive x cutoff x resonance x key mode,
 *   the resonant peak, constant-time slide, accent charging, 303 vs KEY
 *   cutoff tracking; and a full-engine render of the boot song through the
 *   real sequencer core (writes WAV files to build/host to listen to).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ipc.h"
#include "seq_core.h"
#include "audio.h"
#include "app.h"
#include "dsp_tables.h"
#include "corrosion.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* app.h symbols the engine files reference */
shared_t *SH;
volatile uint16_t ui_entropy;
uint16_t hw_noise(void) { return 0; }
void app_send(uint32_t cmd) { seq_command(cmd); }
uint8_t app_next_bar_pattern(void) { return seq_next_bar_pattern(); }

static void write_wav(const char *path, const int16_t *lr, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t data = frames * 4, riff = 36 + data, fl = 16, sr = 32728, br = 32728 * 4;
    uint16_t fmt = 1, ch = 2, ba = 4, bits = 16;
    if (!f) return;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(lr, 4, frames, f);
    fclose(f);
}

static int32_t acid_tick(int32_t *buf)
{
    memset(buf, 0, CTRL_LEN * sizeof(int32_t));
    acid_render(buf);
    return 0;
}

/* 1. stability everywhere */
static void test_stability(void)
{
    static int32_t buf[CTRL_LEN];
    int w, d, c, r, k, t, i;
    int bad = 0, worst_state = 0;
    long long worst_out = 0;
    for (k = 0; k < 2; k++)
    for (w = 0; w < 4; w++)
    for (d = 0; d < 4; d++)
    for (c = 0; c <= 127; c += 127)
    for (r = 0; r <= 127; r += 63) {
        acid_init();
        acid_p.wave = (uint8_t)w; acid_p.drive = (uint8_t)d; acid_p.drive_amt = 127;
        acid_p.cutoff = (uint8_t)c; acid_p.res = (uint8_t)r; acid_p.keytrack = (uint8_t)k;
        acid_p.envmod = 127; acid_p.accent = 127; acid_p.decay = 64; acid_p.slide = 40; acid_p.level = 127;
        for (t = 0; t < 2000; t++) {                     /* ~2 s */
            if (t % 120 == 0) acid_note((uint8_t)((t / 120) * 7 % 48), (uint8_t)((t / 120) & 1));
            if (t % 120 == 60) acid_glide((uint8_t)((t / 120) * 5 % 48), 1);
            if (t % 480 == 100) acid_gate_off();
            acid_tick(buf);
            for (i = 0; i < CTRL_LEN; i++) {
                long long a = buf[i] < 0 ? -(long long)buf[i] : buf[i];
                if (a > worst_out) worst_out = a;
            }
            {
                int s[4] = { acid.y1, acid.y2, acid.y3, acid.y4 }, j;
                for (j = 0; j < 4; j++) { int a = abs(s[j]); if (a > worst_state) worst_state = a; if (a > Q14_ONE + 64) bad++; }
            }
        }
    }
    CHECK(bad == 0, "ladder states left +-1.0 (%d times, worst %d)", bad, worst_state);
    CHECK(worst_out < 140000, "ACID output bounded (worst %lld)", worst_out);
    printf("  stability: 256 settings x 2 s, worst ladder state %d (1.0 = 16384), worst output %lld\n",
           worst_state, worst_out);
}

/* magnitude of a frequency in a rendered buffer (Goertzel) */
static double tone_mag(const int32_t *x, int n, double hz)
{
    double w = 2 * M_PI * hz / 32728.5, c = 2 * cos(w), s1 = 0, s2 = 0;
    int i;
    for (i = 0; i < n; i++) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

/* 2. resonance makes a peak at the cutoff (303 mode: fc fixed) */
static void test_resonance(void)
{
    static int32_t buf[32768];
    double peak[2], fc;
    int r, t;
    for (r = 0; r < 2; r++) {
        acid_init();
        acid_p.wave = 0; acid_p.drive = DRV_OFF; acid_p.keytrack = CUT_303;
        acid_p.cutoff = 60; acid_p.res = r ? 120 : 0; acid_p.envmod = 0; acid_p.accent = 0;
        acid_p.decay = 127; acid_p.level = 127; acid_p.tune = 0;
        evo_cut_ofs = 0;
        acid_note(0, 0);                               /* C1: dense harmonics */
        memset(buf, 0, sizeof buf);
        for (t = 0; t < 1024; t++) acid_render(buf + t * CTRL_LEN);
        fc = 32.7032 * pow(2, (470 + 60 * 42) / 768.0);
        {   /* strongest harmonic within a third of an octave of fc */
            double best = 0, h;
            for (h = 32.7032; h < fc * 1.26; h += 32.7032) if (h > fc / 1.26) { double m = tone_mag(buf + 4096, 16384, h); if (m > best) best = m; }
            peak[r] = best;
        }
    }
    CHECK(peak[1] > peak[0] * 4, "resonant peak +%.1f dB at fc", 20 * log10(peak[1] / peak[0]));
    printf("  resonance: harmonics near fc %.1f dB louder at RES 120 than RES 0\n", 20 * log10(peak[1] / peak[0]));
}

/* 3. slide time is constant: any interval takes SLIDE ticks */
static void test_slide(void)
{
    static int32_t buf[CTRL_LEN];
    static const int iv[] = { 1, 5, 12, 24, -7, -19 };
    unsigned i;
    int s;
    for (s = 0; s <= 127; s += 42) {
        for (i = 0; i < sizeof iv / sizeof iv[0]; i++) {
            int ticks = 0, from = 24, to = 24 + iv[i];
            acid_init();
            acid_p.slide = (uint8_t)s; acid_p.decay = 127;
            acid_note((uint8_t)from, 0);
            acid_tick(buf);
            acid_glide((uint8_t)to, 0);
            {
                int prev = acid.pitch, mono = 1;
                while (acid.pitch != (to << 6) && ticks < 1000) {
                    acid_tick(buf); ticks++;
                    if ((to > from && acid.pitch < prev) || (to < from && acid.pitch > prev)) mono = 0;
                    if ((to > from && acid.pitch > (to << 6)) || (to < from && acid.pitch < (to << 6))) mono = 0;
                    prev = acid.pitch;
                }
                CHECK(mono, "slide %d, %+d: monotonic, no overshoot", s, iv[i]);
            }
            CHECK(ticks == slide_ticks[s], "slide %d, %+d semitones: %d ticks (want %d)", s, iv[i], ticks, slide_ticks[s]);
            CHECK(acid.held && acid.stage != 0, "gate held through the slide");
        }
    }
    printf("  slide: constant time for every interval (e.g. SLIDE 42 = %d ticks = %.0f ms)\n",
           slide_ticks[42], slide_ticks[42] * 32 / 32.7285);
}

/* 4. accents charge: a run of accents gets brighter and brighter */
static void test_accent_charge(void)
{
    static int32_t buf[CTRL_LEN];
    int n, t, peak[6];
    acid_init();
    acid_p.cutoff = 30; acid_p.envmod = 40; acid_p.accent = 110; acid_p.decay = 50;
    evo_cut_ofs = 0;
    for (n = 0; n < 6; n++) {
        acid_note(12, 1);
        peak[n] = 0;
        for (t = 0; t < 116; t++) {                    /* one 16th at 132 BPM */
            acid_tick(buf);
            if (t % 116 == 0) acid_step();
            if (acid.dbg_cut > peak[n]) peak[n] = acid.dbg_cut;
        }
    }
    CHECK(peak[1] > peak[0] && peak[2] > peak[1] && peak[3] > peak[2], "accent run builds: %d %d %d %d",
          peak[0], peak[1], peak[2], peak[3]);
    printf("  accent charge: peak cutoff over a run of accents (1/64 semitone) %d %d %d %d %d %d\n",
           peak[0], peak[1], peak[2], peak[3], peak[4], peak[5]);
}

/* 5. 303 mode ignores the note; KEY mode follows it */
static void test_keytrack(void)
{
    static int32_t buf[CTRL_LEN];
    int k, c[2][2], n, t;
    for (k = 0; k < 2; k++)
        for (n = 0; n < 2; n++) {
            acid_init();
            acid_p.keytrack = (uint8_t)k; acid_p.cutoff = 50; acid_p.decay = 0;
            evo_cut_ofs = 0;
            acid_note((uint8_t)(n ? 24 : 0), 0);
            for (t = 0; t < 600; t++) acid_tick(buf);  /* envelopes settled */
            c[k][n] = acid.dbg_cut;
        }
    CHECK(c[0][0] == c[0][1], "303 mode: cutoff independent of the note (%d %d)", c[0][0], c[0][1]);
    CHECK(c[1][1] - c[1][0] == 24 * 64, "KEY mode: cutoff follows the note (%d)", c[1][1] - c[1][0]);
}

/* 6. the whole engine: the boot song through the real sequencer and renderer */
static void test_engine(void)
{
    static shared_t sh;
    static int16_t ol[AUDIO_BLOCK], or_[AUDIO_BLOCK];
    static int16_t lr[2 * 32768 * 40];
    static int16_t stem[2 * 32768 * 12];
    uint32_t blk, frames = 0, nblk = (uint32_t)(32728.5 * 36 / AUDIO_BLOCK), i;
    int32_t peak = 0;
    double sum = 0;
    memset(&sh, 0, sizeof sh);
    SH = &sh;
    demo_load(&sh.proj);
    demo_import_sound(&sh.proj);
    {   /* the boot pattern is exactly what CORROSION makes from the code */
        pattern_t ref;
        gen_t g;
        memset(&g, 0, sizeof g);
        corr_code_decode(&g, DEMO_CODE);
        pattern_clear(&ref);
        corr_generate(&ref, &g, GEN_ALL);
        CHECK(!memcmp(&ref, &sh.proj.pat[0], sizeof ref), "boot pattern = CORROSION(0F1C-9009-5870)");
    }
    audio_engine_init();
    foundry_build();
    seq_init(&sh);
    seq_command(CMD(CMD_START, 0xBEEF));
    evo_seed(0xBEEF);
    for (blk = 0; blk < nblk; blk++) {
        seq_render_block(blk);
        audio_render_block(&sh.ev[blk & (AUDIO_NBLK - 1)], blk, ol, or_);
        for (i = 0; i < AUDIO_BLOCK; i++, frames++) {
            lr[2 * frames] = ol[i];
            lr[2 * frames + 1] = or_[i];
            if (abs(ol[i]) > peak) peak = abs(ol[i]);
            if (abs(or_[i]) > peak) peak = abs(or_[i]);
            sum += (double)ol[i] * ol[i];
        }
        if ((blk & 31) == 0) evo_frame();         /* the main loop, ~8 Hz is plenty */
    }
    write_wav("../../build/host/engine_boot_song.wav", lr, frames);
    CHECK(peak <= LIMIT_CEIL + 64, "limiter holds the ceiling (peak %d)", peak);
    CHECK(sh.evo_commits >= 3, "DRIFT evolved (%u commits)", sh.evo_commits);
    printf("  engine: %.1f s of the boot song, rms %.1f dBFS, peak %d, %u evolutions -> build/host/engine_boot_song.wav\n",
           frames / 32728.5, 10 * log10(sum / frames / (32768.0 * 32768.0)), peak, sh.evo_commits);

    /* ACID alone (no drums, no FX) for listening to the voice */
    {
        static int32_t a[CTRL_LEN];
        uint32_t f = 0, t;
        int w;
        acid_init();
        demo_import_sound(&sh.proj);
        for (w = 0; w < 4; w++) {
            const pattern_t *p = &sh.proj.pat[0];
            int s, rep;
            acid_p.wave = (uint8_t)w;
            acid_p.drive = (uint8_t)w;                    /* one drive mode per wave */
            for (rep = 0; rep < 2; rep++)
                for (s = 0; s < 16; s++) {
                    uint8_t fl = p->flags[s];
                    if ((fl & 3) == STEP_NOTE) acid_note(p->note[s], fl & STEP_ACCENT);
                    for (t = 0; t < 124; t++) {
                        if (t == 70 && !(fl & STEP_SLIDE)) acid_gate_off();
                        memset(a, 0, sizeof a);
                        acid_render(a);
                        for (i = 0; i < CTRL_LEN && f < 32768 * 12; i++, f++) {
                            int32_t v = a[i] > 32767 ? 32767 : a[i] < -32767 ? -32767 : a[i];
                            stem[2 * f] = stem[2 * f + 1] = (int16_t)v;
                        }
                    }
                    acid_step();
                }
        }
        write_wav("../../build/host/acid_waves_drives.wav", stem, f);
        printf("  ACID stem: SAW/OFF, SQUARE/SOFT, HOLLOW/HARD, BITE/FOLD -> build/host/acid_waves_drives.wav\n");
    }
}

int main(void)
{
    test_stability();
    test_resonance();
    test_slide();
    test_accent_charge();
    test_keytrack();
    test_engine();
    printf("test_dsp: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
