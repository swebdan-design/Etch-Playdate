/*
 * ETCH DS -- by Synth Centric
 * FOUNDRY, minimal milestone-1 version: six drum lanes.
 *
 * Each sound is synthesised from a handful of parameters and rendered into a
 * sample buffer when it is (re)built -- at boot for now; later, whenever a
 * sound is edited.  Playback in the audio interrupt is then just a sample
 * player per lane (varispeed with linear interpolation only when the lane is
 * re-tuned, e.g. by the living-song tuning drift).
 *
 * Lanes 0-3 are ETCH's four lanes with its kits as presets (KICK: DEEP PUNCH
 * THUD CLICK, SNARE: SHORT GRIT CLAP RIM, CLOSED HAT: CLOSD TICK SHAKE SHARP,
 * OPEN HAT: OPEN METAL BLIP CRASH).  Lanes 4 (METAL) and 5 (NOISE) are new;
 * ETCH patterns never write them, they exist so the engine and the CPU
 * budget already carry six lanes.
 *
 * Rendering runs in the main loop (boot), not on the audio path; it uses
 * integer arithmetic; integer division only to set up each sound's
 * constants, never per sample.  The playback path below uses neither division nor floats.
 */
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "dsp_tables.h"
#ifdef __NDS__
#include <nds.h>
#endif

int8_t evo_tune_ofs[DS_LANES];
uint8_t foundry_tune[DS_LANES]  = { 8, 8, 8, 8, 8, 8 };
uint8_t foundry_decay[DS_LANES] = { 8, 8, 8, 8, 8, 8 };
uint8_t foundry_level[DS_LANES] = { 15, 15, 15, 15, 15, 15 };

const char * const foundry_kit_names[DS_LANES][KITS_PER_LANE] = {
    { "DEEP ", "PUNCH", "THUD ", "CLICK" },
    { "SHORT", "GRIT ", "CLAP ", "RIM  " },
    { "CLOSD", "TICK ", "SHAKE", "SHARP" },
    { "OPEN ", "METAL", "BLIP ", "CRASH" },
    { "CLANG", "CLANG", "CLANG", "CLANG" },
    { "BURST", "BURST", "BURST", "BURST" },
};

/* ---- sound definitions: the presets, and the editable copy ------------------ */
const char * const foundry_model_names[DT_COUNT] = {
    "KICK", "SNARE", "CLAP", "RIM", "HAT", "METAL", "BLIP", "RING", "CRUSH"
};

const drum_def_t foundry_presets[DS_LANES][KITS_PER_LANE] = {
    {   /* KICK */
        { DT_KICK, 150, 44, 28, 520, 0, 40, 520 },      /* DEEP  */
        { DT_KICK, 230, 52, 12, 300, 60, 150, 320 },    /* PUNCH */
        { DT_KICK, 120, 50, 30, 200, 0, 90, 230 },      /* THUD  */
        { DT_KICK, 320, 58, 6, 240, 200, 60, 260 },     /* CLICK */
    },
    {   /* SNARE / CLAP */
        { DT_SNARE, 190, 165, 30, 140, 170, 40, 200 },  /* SHORT */
        { DT_SNARE, 160, 140, 40, 260, 220, 140, 300 }, /* GRIT  */
        { DT_CLAP, 1100, 0, 0, 220, 255, 30, 320 },     /* CLAP  */
        { DT_RIM, 1700, 820, 0, 40, 0, 60, 60 },        /* RIM   */
    },
    {   /* CLOSED HAT */
        { DT_HAT, 7000, 0, 0, 55, 255, 0, 80 },         /* CLOSD */
        { DT_METALHAT, 6000, 0, 0, 40, 120, 0, 60 },    /* TICK  */
        { DT_HAT, 4500, 0, 4, 110, 200, 0, 140 },       /* SHAKE */
        { DT_HAT, 9000, 0, 0, 22, 255, 0, 40 },         /* SHARP */
    },
    {   /* OPEN HAT */
        { DT_HAT, 6500, 0, 0, 420, 255, 0, 480 },       /* OPEN  */
        { DT_METALHAT, 5000, 0, 0, 480, 160, 0, 540 },  /* METAL */
        { DT_BLIP, 620, 380, 25, 150, 0, 0, 180 },      /* BLIP  */
        { DT_HAT, 4000, 0, 0, 1000, 255, 30, 1100 },    /* CRASH */
    },
    {   /* METAL (DS) */
        { DT_RING, 523, 769, 0, 380, 40, 120, 420 },
        { DT_RING, 523, 769, 0, 380, 40, 120, 420 },
        { DT_RING, 523, 769, 0, 380, 40, 120, 420 },
        { DT_RING, 523, 769, 0, 380, 40, 120, 420 },
    },
    {   /* NOISE (DS) */
        { DT_CRUSH, 2500, 0, 0, 260, 255, 160, 300 },
        { DT_CRUSH, 2500, 0, 0, 260, 255, 160, 300 },
        { DT_CRUSH, 2500, 0, 0, 260, 255, 160, 300 },
        { DT_CRUSH, 2500, 0, 0, 260, 255, 160, 300 },
    },
};

drum_def_t foundry_defs[DS_LANES][KITS_PER_LANE];
static dvoice_t dv[DS_LANES] AUDIO_BSS;
static int16_t *smp[DS_LANES][KITS_PER_LANE];
static uint32_t smp_len[DS_LANES][KITS_PER_LANE];

/* ---- render helpers (boot time) -------------------------------------------- */
static uint32_t lfsr = 0xACE1u;
static inline int32_t noise(void)
{
    lfsr ^= lfsr << 13; lfsr ^= lfsr >> 17; lfsr ^= lfsr << 5;
    return (int32_t)(int16_t)(lfsr >> 8);
}

/* per-sample multiplier (Q24) for a decay of e^-6 over ms milliseconds:
 * exp(-6 / n) ~ 1 - x + x^2/2 - x^3/6 with x = 6/n (n samples, n >= 50) */
static uint32_t decay_q24(uint32_t ms)
{
    uint32_t n = ms * (FS_HZ / 1000u);
    uint64_t x;
    if (n < 50) n = 50;
    x = ((uint64_t)6 << 24) / n;                     /* Q24 */
    return (uint32_t)((1u << 24) - x + ((x * x) >> 25) - ((((x * x) >> 24) * x) / 6 >> 24));
}

/* phase increment (32 bits/cycle) for f Hz */
static uint32_t hz_inc(uint32_t hz)
{
    return (uint32_t)(((uint64_t)hz << 32) / FS_HZ);
}

/* one-pole coefficient (Q16) for a corner at hz: 2 pi f / fs, clamped */
static int32_t hz_g(uint32_t hz)
{
    uint32_t g = (uint32_t)(((uint64_t)hz * 411775u) / FS_HZ);   /* 2pi*65536 */
    return g > 60000u ? 60000 : (int32_t)g;
}

static inline int32_t sine(uint32_t ph) { return sine_tab[ph >> 22]; }

static inline int32_t softclip(int32_t x, uint32_t drive)
{
    /* x Q15; gain 1..5 then cubic saturation (evaluated in Q14) */
    int32_t u, u2, u3;
    x = (x * (int32_t)(256 + drive * 4)) >> 8;
    u = x >> 1;
    if (u > 24576) u = 24576;
    if (u < -24576) u = -24576;
    u2 = (u * u) >> 14;
    u3 = (u2 * u) >> 14;
    return (u - ((u3 * 2427) >> 14)) << 1;
}

static void render(int16_t *out, uint32_t n, const drum_def_t *d, uint32_t decay_scale_q8)
{
    uint32_t i, amp = 1u << 24, pamp = 1u << 24;
    uint32_t ad = decay_q24((d->amp_ms * decay_scale_q8) >> 8);
    uint32_t pd = decay_q24(d->pitch_ms ? d->pitch_ms : 1);
    uint32_t ph = 0, ph2 = 0, ph3 = 0;
    int32_t hp_lp = 0, lp = 0, hold = 0;
    int32_t g = hz_g(d->f0), g_hi = hz_g(d->f0 * 2u), g_lo = hz_g(d->f0 / 2u);
    uint32_t inc0 = hz_inc(d->f0), inc1 = hz_inc(d->f1), inc3 = hz_inc(d->f1 * 3u / 2u);
    uint32_t spms = FS_HZ / 1000u, ms = 0, ms_cnt = 0, hold_cnt = 0;
    uint32_t attack = (uint32_t)d->pitch_ms * spms;
    int32_t attack_step = attack ? (int32_t)(32767u / attack) : 0;
    int32_t tone_mix = 255 - d->noise, noise_mix = (d->noise * 298) >> 8;
    /* 808-style metallic cluster (ratios of six square waves) */
    static const uint16_t metal_hz[6] = { 205, 304, 370, 523, 540, 800 };
    uint32_t mph[6] = { 0 }, minc[6];
    for (i = 0; i < 6; i++) minc[i] = hz_inc(metal_hz[i] * (d->f0 / 1000u + 1u));

    for (i = 0; i < n; i++) {
        int32_t s = 0, a = (int32_t)(amp >> 9);     /* Q15 */
        switch (d->type) {
        case DT_KICK: {
            uint32_t inc = inc1 + (uint32_t)(((uint64_t)(inc0 - inc1) * pamp) >> 24);
            s = sine(ph);
            ph += inc;
            if (ms < 3) s += (noise() * d->noise) >> 8;                     /* click */
            s = softclip(s, d->drive);
            break; }
        case DT_SNARE: {
            int32_t tone, nz = noise();
            uint32_t inc = inc1 + (uint32_t)(((uint64_t)(inc0 - inc1) * pamp) >> 24);
            tone = (sine(ph) + (sine(ph2) >> 1)) >> 1;
            ph += inc; ph2 += inc + (inc >> 1) + (inc >> 3);
            if (d->drive > 100) {                    /* GRIT: sample-held noise */
                if ((i & 3) == 0) hold = nz;
                nz = hold;
            }
            hp_lp += ((nz - hp_lp) * 9000) >> 16;
            nz -= hp_lp;
            {
                int32_t tone_a = (int32_t)(((uint64_t)pamp * 32767) >> 24);
                s = ((((tone * tone_a) >> 15) * tone_mix) >> 8) + ((nz * noise_mix) >> 8);
            }
            s = softclip(s, d->drive);
            break; }
        case DT_CLAP: {
            int32_t nz = noise(), env;
            lp += ((nz - lp) * g_hi) >> 16;
            hp_lp += ((lp - hp_lp) * g_lo) >> 16;
            nz = (lp - hp_lp) * 2;
            if (ms < 30) {                          /* three bursts, 10 ms apart */
                uint32_t t = ms < 10 ? ms : ms < 20 ? ms - 10 : ms - 20;
                env = 32767 - (int32_t)t * 3000;
                if (env < 0) env = 0;
                s = (nz * env) >> 15;
                a = 32767;
            } else {
                s = nz;
            }
            break; }
        case DT_RIM: {
            int32_t sq1 = (ph >> 29) == 0 ? 20000 : -6000;          /* 12.5 % pulse */
            int32_t sq2 = (ph2 >> 30) == 0 ? 12000 : -12000;
            ph += inc0; ph2 += inc1;
            s = sq1 + (sq2 >> 1);
            break; }
        case DT_HAT:
        case DT_METALHAT: {
            int32_t nz = noise();
            if (d->type == DT_METALHAT) {
                int32_t m = 0, j;
                for (j = 0; j < 6; j++) { m += (mph[j] >> 31) ? 5000 : -5000; mph[j] += minc[j]; }
                nz = (nz * (255 - d->noise) + m * d->noise) >> 8;
            }
            hp_lp += ((nz - hp_lp) * g) >> 16;      /* high-pass */
            s = nz - hp_lp;
            if (i < attack) s = (s * (int32_t)(i * (uint32_t)attack_step)) >> 15;   /* SHAKE */
            if (d->drive) s = softclip(s, d->drive);
            break; }
        case DT_BLIP: {
            uint32_t inc = inc1 + (uint32_t)(((uint64_t)(inc0 - inc1) * pamp) >> 24);
            s = (ph >> 31) ? 16000 : -16000;
            s = (s + sine(ph)) >> 1;
            ph += inc;
            break; }
        case DT_RING: {
            int32_t a1 = (ph >> 31) ? 32767 : -32767;
            int32_t a2 = (ph2 >> 31) ? 32767 : -32767;
            int32_t a3 = sine(ph3);
            ph += inc0; ph2 += inc1; ph3 += inc3;
            s = (int32_t)(((int64_t)a1 * a2) >> 16) + ((a3 * (int32_t)d->noise) >> 9);
            s = softclip(s, d->drive);
            break; }
        case DT_CRUSH: {
            if (hold_cnt == 0) { hold = noise() & ~0x0FFF; hold_cnt = 6; }   /* 4-bit, fs / 6 */
            hold_cnt--;
            lp += ((hold - lp) * g) >> 16;
            s = softclip(lp * 2, d->drive);
            break; }
        default:
            break;
        }
        s = (s * a) >> 15;
        if (s > 32767) s = 32767;
        if (s < -32767) s = -32767;
        out[i] = (int16_t)s;
        if (++ms_cnt >= spms) { ms_cnt = 0; ms++; }
        amp = (uint32_t)(((uint64_t)amp * ad) >> 24);
        pamp = (uint32_t)(((uint64_t)pamp * pd) >> 24);
    }
    /* 2 ms fade-out (64 samples) so a truncated tail never clicks */
    {
        uint32_t f = 64, j;
        if (f > n) f = n;
        for (j = 0; j < f; j++) out[n - 1 - j] = (int16_t)((out[n - 1 - j] * (int32_t)j) >> 6);
    }
}

/* Re-render one sound from foundry_defs and swap it in.  The new sample is
 * rendered first; the swap happens with interrupts off, and any voice still
 * playing the old buffer is silenced before that buffer is freed, so an edit
 * can never leave the audio interrupt reading freed memory. */
void foundry_rebuild(uint32_t l, uint32_t k)
{
    drum_def_t *d = &foundry_defs[l][k];
    int32_t dd = (int32_t)foundry_decay[l] - 8;
    uint32_t scale = (uint32_t)(256 + dd * 40), n, v;
    int16_t *nb, *old;
    if (l >= DS_LANES || k >= KITS_PER_LANE) return;
    if (l >= 4) k = 0;                                 /* the new lanes have one sound */
    if (d->type >= DT_COUNT) d->type = DT_KICK;
    n = ((uint32_t)d->len_ms * scale >> 8) * (FS_HZ / 1000u);
    if (n < 256) n = 256;
    if (n > FS_HZ * 2u) n = FS_HZ * 2u;
    nb = (int16_t *)malloc(n * sizeof(int16_t));
    if (!nb) return;
    render(nb, n, d, scale);
    old = smp[l][k];
    {
#ifdef __NDS__
        int ime = enterCriticalSection();
#endif
        uint32_t kk;
        for (kk = 0; kk < KITS_PER_LANE; kk++)
            if (kk == k || (l >= 4)) { smp[l][kk] = nb; smp_len[l][kk] = n; }
        for (v = 0; v < DS_LANES; v++)
            if (dv[v].active && dv[v].smp == old) dv[v].active = 0;
#ifdef __NDS__
        leaveCriticalSection(ime);
#endif
    }
    if (old) free(old);
}

/* derived length: the decay to e^-6 plus a short tail */
void foundry_set_len(drum_def_t *d)
{
    uint32_t ms = (uint32_t)d->amp_ms + d->amp_ms / 8u + 20u;
    d->len_ms = (uint16_t)(ms > 2000u ? 2000u : ms);
}

void foundry_build(void)
{
    uint32_t l, k;
    static uint8_t init;
    if (!init) { memcpy(foundry_defs, foundry_presets, sizeof foundry_defs); init = 1; }
    for (l = 0; l < DS_LANES; l++)
        for (k = 0; k < (l >= 4 ? 1u : KITS_PER_LANE); k++) foundry_rebuild(l, k);
}

const int16_t *foundry_sample(uint32_t l, uint32_t k, uint32_t *len)
{
    if (l >= DS_LANES || k >= KITS_PER_LANE) { *len = 0; return 0; }
    *len = smp_len[l][k];
    return smp[l][k];
}

/* ---- playback (audio interrupt) --------------------------------------------- */
/* pan (Q15 left, right): kick and snare centre, hats and new lanes spread */
static const int16_t pan_l[DS_LANES] = { 23170, 23170, 17000, 28000, 30000, 15000 };
static const int16_t pan_r[DS_LANES] = { 23170, 23170, 28000, 17000, 15000, 30000 };

AUDIO_CODE void foundry_trigger(uint8_t lanes, uint8_t kits)
{
    uint32_t l;
    if (lanes & (1u << LANE_CH)) {                  /* closed hat chokes open hat */
        dvoice_t *o = &dv[LANE_OH];
        if (o->active) o->fade = (o->gain >> 6) + 1;
    }
    for (l = 0; l < DS_LANES; l++) {
        dvoice_t *v;
        uint32_t kit;
        int32_t t;
        if (!(lanes & (1u << l))) continue;
        v = &dv[l];
        kit = l < 4 ? (kits >> (l * 2)) & 3 : 0;
        v->smp = smp[l][kit];
        v->len = smp_len[l][kit];
        v->pos = 0;
        t = (int32_t)foundry_tune[l] - 8 + evo_tune_ofs[l];
        if (t < -24) t = -24;
        if (t > 24) t = 24;
        v->rate = semi_ratio[t + 24];
        v->gain = ((int32_t)foundry_level[l] + 1) << 10;      /* Q15, full level = 0.5 */
        v->fade = 0;
        v->active = v->smp != 0 && v->len != 0;
    }
}

AUDIO_CODE void foundry_kill(void)
{
    uint32_t l;
    for (l = 0; l < DS_LANES; l++) if (dv[l].active) dv[l].fade = (dv[l].gain >> 6) + 1;
}

AUDIO_CODE void foundry_render(int32_t *L, int32_t *R, uint32_t n)
{
    uint32_t l, i;
    for (l = 0; l < DS_LANES; l++) {
        dvoice_t *v = &dv[l];
        const int16_t *s;
        uint32_t pos, len, rate;
        int32_t gain, gl, gr, fade;
        if (!v->active) continue;
        s = v->smp; pos = v->pos; len = v->len; rate = v->rate;
        gain = v->gain; fade = v->fade;
        gl = pan_l[l]; gr = pan_r[l];
        for (i = 0; i < n; i++) {
            uint32_t ip = pos >> 16;
            int32_t x, m;
            if (ip + 1 >= len) { v->active = 0; break; }
            if (rate == 65536u) x = s[ip];
            else {
                int32_t a = s[ip], b = s[ip + 1];
                x = a + (((b - a) * (int32_t)((pos >> 1) & 0x7FFF)) >> 15);
            }
            pos += rate;
            m = (x * gain) >> 15;
            L[i] += (m * gl) >> 15;
            R[i] += (m * gr) >> 15;
            if (fade) {
                gain -= fade;
                if (gain <= 0) { v->active = 0; break; }
            }
        }
        v->pos = pos;
        v->gain = gain;
    }
}
