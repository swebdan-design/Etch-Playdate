/*
 * ETCH CRANK -- by Synth Centric
 * The resampler: ETCH DS's 32 728.498 Hz engine rate to the Playdate's
 * 44 100 Hz, through the real pump (pump_source feeds test signals in place
 * of the engine).
 *
 *   gain       DC is exact; tones are measured for passband flatness
 *   images     the strongest spurious component next to each tone
 *   rate       a minute of output consumes exactly a minute of engine audio
 *              (the tempo is ETCH DS's, to the sample)
 *   cost       is measured on the device with the meter (RESAMPLE)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ipc.h"
#include "audio.h"
#include "app_glue.h"
#include "pump.h"
#include "resample.h"

uint16_t hw_noise(void) { return 1; }

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
                           printf(__VA_ARGS__); printf("\n"); } } while (0)

static double src_hz, src_amp;
static uint64_t src_n;
static void source(int16_t *l, int16_t *r)
{
    uint32_t i;
    for (i = 0; i < AUDIO_BLOCK; i++, src_n++) {
        double v = src_amp * (src_hz > 0 ? sin(2 * M_PI * src_hz * (double)src_n / (16756991.0 / 512)) : 1.0);
        l[i] = r[i] = (int16_t)lrint(v * 32767);
    }
}

static void reset(double hz, double amp)
{
    shared_init();
    pump_init(SH);
    pump_source = source;
    src_hz = hz;
    src_amp = amp;
    src_n = 0;
}

#define N 32768
static int16_t out_l[N], out_r[N];

/* magnitude at hz of out_l[skip..skip+len) (Blackman-Harris window) */
static double mag_at(double hz, int skip, int len)
{
    double re = 0, im = 0, wsum = 0;
    int i;
    for (i = 0; i < len; i++) {
        double t = (double)i / (len - 1);
        double w = 0.35875 - 0.48829 * cos(2 * M_PI * t) + 0.14128 * cos(4 * M_PI * t) - 0.01168 * cos(6 * M_PI * t);
        re += out_l[skip + i] * w * cos(2 * M_PI * hz * i / 44100.0);
        im -= out_l[skip + i] * w * sin(2 * M_PI * hz * i / 44100.0);
        wsum += w;
    }
    return 2 * sqrt(re * re + im * im) / wsum / 32767.0;
}

int main(void)
{
    int i;
    audio_engine_init();

    /* DC: unity, to within the coefficient interpolation's rounding (1 LSB) */
    reset(0, 0.5);
    pump_pull(out_l, out_r, 4096);
    {
        int ok = 1, lo = 32767, hi = -32768;
        for (i = 64; i < 4096; i++) {
            if (abs(out_l[i] - 16384) > 2 || out_r[i] != out_l[i]) ok = 0;
            if (out_l[i] < lo) lo = out_l[i];
            if (out_l[i] > hi) hi = out_l[i];
        }
        CHECK(ok, "DC should pass at unity (%d..%d)", lo, hi);
        printf("  DC: in 16384 -> out %d..%d\n", lo, hi);
    }

    /* tones: gain and the worst spurious component */
    {
        static const double tones[] = { 100, 1000, 5000, 10000, 13000, 15000 };
        for (i = 0; i < 6; i++) {
            double g, worst = 0, worst_hz = 0, f;
            reset(tones[i], 0.5);
            pump_pull(out_l, out_r, N);
            g = mag_at(tones[i], 2048, 16384) / 0.5;
            /* scan 20 Hz .. 22 kHz away from the tone */
            for (f = 20; f < 22000; f += 37) {
                double m;
                if (fabs(f - tones[i]) < 400) continue;
                m = mag_at(f, 2048, 16384) / 0.5;
                if (m > worst) { worst = m; worst_hz = f; }
            }
            printf("  %5.0f Hz: gain %+.2f dB, worst spurious %.1f dB at %.0f Hz\n",
                   tones[i], 20 * log10(g), 20 * log10(worst + 1e-12), worst_hz);
            if (tones[i] <= 10000) CHECK(fabs(20 * log10(g)) < 0.2, "passband gain at %.0f Hz", tones[i]);
            if (tones[i] <= 10000) CHECK(20 * log10(worst + 1e-12) < -60, "spurious near %.0f Hz", tones[i]);
        }
    }

    /* rate: one minute of output = one minute of engine audio */
    {
        uint32_t total = 0;
        double expect, got;
        reset(0, 0);
        while (total < 44100u * 60u) { pump_pull(out_l, out_r, 441); total += 441; }
        expect = 60.0 * 16756991.0 / 512.0;
        got = (double)pump_blocks() * AUDIO_BLOCK;
        printf("  one minute at 44.1 kHz consumed %.0f engine samples (exact: %.1f; the difference is the %d-frame look-ahead and block rounding)\n",
               got, expect, RS_TAPS / 2);
        CHECK(got >= expect && got - expect < AUDIO_BLOCK + RS_TAPS, "rate");
        printf("  step %u / 2^32 = %.12f engine samples per output sample (exact %.12f)\n",
               rs_step_frac, rs_step_frac / 4294967296.0, 16756991.0 / 512 / 44100);
    }
    printf("test_resample: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
