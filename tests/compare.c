/*
 * ETCH CRANK -- by Synth Centric
 * Compares two engine-rate renders (interleaved L/R int16 at 32 728.5 Hz) and
 * says exactly where they first differ: sample, block, time, bar and step at
 * the song's tempo, channel and values, plus how much of the song differs.
 *
 *   compare A.raw B.raw [BPM]        exit 0 = bit-identical
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static int16_t *load(const char *p, long *n)
{
    FILE *f = fopen(p, "rb");
    int16_t *b;
    if (!f) { printf("cannot open %s\n", p); exit(2); }
    fseek(f, 0, SEEK_END);
    *n = ftell(f) / 2;
    fseek(f, 0, SEEK_SET);
    b = (int16_t *)malloc((size_t)*n * 2 + 2);
    if (fread(b, 2, (size_t)*n, f) != (size_t)*n) { printf("short read %s\n", p); exit(2); }
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    long na, nb, n, i, first = -1, last = -1, diffs = 0;
    int16_t *a, *b;
    double bpm = argc > 3 ? atof(argv[3]) : 124.0, fs = 32728.498;
    if (argc < 3) { printf("usage: compare A.raw B.raw [BPM]\n"); return 2; }
    a = load(argv[1], &na);
    b = load(argv[2], &nb);
    n = na < nb ? na : nb;
    for (i = 0; i < n; i++)
        if (a[i] != b[i]) { if (first < 0) first = i; last = i; diffs++; }
    if (first < 0 && na == nb) {
        printf("  bit-identical: %ld frames (%.2f s), every sample of both channels\n", n / 2, n / 2 / fs);
        return 0;
    }
    if (first < 0) {
        printf("  DIFFER in length only: %ld vs %ld frames (identical over the common %ld)\n", na / 2, nb / 2, n / 2);
        return 1;
    }
    {
        long fr = first / 2;
        double t = fr / fs, step_len = fs * 60.0 / bpm / 4.0;
        long st = (long)(fr / step_len);
        printf("  DIFFER: first at frame %ld (%s channel), block %ld + %ld, %.4f s,"
               " bar %ld step %ld (+%ld samples into the step)\n",
               fr, (first & 1) ? "right" : "left", fr / 128, fr % 128, t, st / 16 + 1, st % 16 + 1,
               fr - (long)(st * step_len));
        printf("          values %d vs %d; %ld of %ld samples differ; last difference at frame %ld (%.4f s)\n",
               a[first], b[first], diffs, n, last / 2, last / 2 / fs);
    }
    return 1;
}
