/*
 * ETCH DS -- by Synth Centric
 * ACID: the 303 voice, synthesised for real.
 *
 *   osc     SAW / SQUARE / HOLLOW / BITE: ETCH's four oscillator recipes as
 *           band-limited wavetables, one mip level per octave, linear
 *           interpolation, 32-bit phase.  The phase increment is interpolated
 *           across each control tick, so slides are smooth per sample.
 *   filter  4-pole resonant ladder (four one-pole stages, feedback k from the
 *           4th), run 2x oversampled per sample with a cubic saturator on the
 *           feedback sum: high resonance rings and self-oscillates but stays
 *           bounded.  Cutoff modes: 303 (absolute, no key tracking) and KEY
 *           (ETCH: the cutoff follows the note).  The coefficient is
 *           interpolated per sample across each control tick.
 *   env     filter envelope: instant attack, exponential DECAY (30 ms..4.5 s);
 *           accented notes snap shut (decay capped), as on a TB-303.
 *   accent  louder VCA peak + a cutoff sweep through an "accent capacitor":
 *           it charges fast and discharges slowly, so a run of accents builds
 *           up brighter and brighter.  ETCH's per-step accent charge (+40 per
 *           accent, decays 1/8 per step) is kept on top.
 *   slide   true constant time: the glide from any interval to any interval
 *           takes exactly SLIDE ticks (linear in pitch), gate held, envelopes
 *           not retriggered; an accent on the slid-into note sweeps without a
 *           reset (ETCH semantics).
 *   VCA     ~1.5 ms attack from the current level (no click on retrigger),
 *           slow decay while held, ~4 ms release.
 *   drive   after the VCA (as a 303 into a pedal): OFF, SOFT (cubic), HARD
 *           (asymmetric clip), FOLD (asymmetric wavefolder); DC blocked.
 *
 * Control rate: one tick per CTRL_LEN = 32 samples (1.02 kHz).  Everything is
 * integer multiply/shift/add; no division, no floating point.
 */
#include "audio.h"
#include "dsp_tables.h"

acid_params_t acid_p = {
    0,     /* SAW              */
    40,    /* cutoff            */
    96,    /* resonance: metallic by default */
    80,    /* env mod           */
    56,    /* decay             */
    80,    /* accent            */
    40,    /* slide (~60 ms)    */
    DRV_SOFT, 48,
    CUT_303,
    0,
    100
};
acid_t acid AUDIO_BSS;
volatile int8_t evo_cut_ofs;

#define ST_IDLE    0
#define ST_ATTACK  1
#define ST_DECAY   2
#define ST_RELEASE 3

#define ACC_PULSE_COEF 63950   /* accent pulse: tau 40 ms per tick (Q16)     */
#define ACC_UP         6000    /* capacitor charge rate (Q16 per tick)       */
#define ACC_DOWN       420     /* capacitor discharge rate: tau ~150 ms      */
#define VCA_ATTACK     40000   /* fraction of the remaining distance / tick  */
#define VCA_RELEASE    51200   /* tau ~4 ms                                  */

/* pitch (1/64 semitone, 0 = C1) -> phase increment; octave via a reciprocal
 * multiply (p / 768 == (p * 21846) >> 24 for p < 2^16, corrected below) */
static inline uint32_t pitch_inc(int32_t p, uint32_t *oct)
{
    int32_t o, r;
    if (p < 0) p = 0;
    if (p > 8 * 768 + 767) p = 8 * 768 + 767;
    o = (p * 21846) >> 24;
    r = p - o * 768;
    if (r >= 768) { r -= 768; o++; }
    if (r < 0) { r += 768; o--; }
    *oct = (uint32_t)o;
    return inc_top[r] >> (8 - o);
}

void acid_init(void)
{
    acid_t *v = &acid;
    v->phase = 0; v->inc = 0;
    v->pitch = v->target = 0; v->pitch_q16 = 0; v->slide_step = 0; v->slide_left = 0;
    v->held = v->active = 0; v->stage = ST_IDLE; v->accented = 0;
    v->env = 0; v->env_coef = dec_coef[64];
    v->acc_env = v->acc_cap = v->charge = 0;
    v->amp = v->peak = 0;
    v->g = cut_g[100]; v->k = 0;
    v->y1 = v->y2 = v->y3 = v->y4 = 0;
    v->dcx = v->dcy = 0;
    v->dbg_cut = 0;
}

static AUDIO_CODE void accent_hit(acid_t *v)
{
    v->acc_env = 65535;
    v->charge += 40;
    if (v->charge > 120) v->charge = 120;
}

AUDIO_CODE void acid_note(uint8_t note, uint8_t accent)
{
    acid_t *v = &acid;
    uint32_t dec = acid_p.decay & 127, oct;
    v->pitch = v->target = (int32_t)note << 6;
    v->slide_left = 0;
    if (!v->active) v->inc = pitch_inc(v->pitch + ((int32_t)acid_p.tune << 6), &oct);
    v->accented = accent ? 1 : 0;
    if (accent) {
        if (dec > 40) dec = 40;                 /* accented notes snap shut */
        v->peak = 32767;
        accent_hit(v);
    } else {
        v->peak = 32767 - ((int32_t)(acid_p.accent & 127) << 7);   /* loudness gap */
    }
    v->env = 65535;
    v->env_coef = dec_coef[dec];
    v->stage = ST_ATTACK;
    v->held = 1;
    v->active = 1;
}

AUDIO_CODE void acid_glide(uint8_t note, uint8_t accent)
{
    acid_t *v = &acid;
    uint32_t n;
    if (!v->held || !v->active) { acid_note(note, accent); return; }
    v->target = (int32_t)note << 6;
    n = slide_ticks[acid_p.slide & 127];
    v->pitch_q16 = v->pitch << 16;
    v->slide_step = (v->target - v->pitch) * (int32_t)slide_recip[acid_p.slide & 127];
    v->slide_left = (uint16_t)n;
    if (v->target == v->pitch) v->slide_left = 0;
    if (accent) {                                /* sweep + loudness, no env reset */
        v->accented = 1;
        v->peak = 32767;
        if (v->stage == ST_DECAY) v->stage = ST_ATTACK;
        accent_hit(v);
    }
}

AUDIO_CODE void acid_gate_off(void)
{
    acid_t *v = &acid;
    v->held = 0;
    if (v->active) v->stage = ST_RELEASE;
}

AUDIO_CODE void acid_step(void)
{
    acid.charge -= acid.charge >> 3;
}

AUDIO_CODE void acid_kill(void)
{
    acid.held = 0;
    if (acid.active) acid.stage = ST_RELEASE;
}

static inline int32_t sat_cubic(int32_t u)
{
    /* y = u - 4/27 u^3 for |u| <= 1.5, +-1 beyond (Q14) */
    int32_t u2, u3;
    if (u > 24576) u = 24576;
    if (u < -24576) u = -24576;
    u2 = (u * u) >> 14;
    u3 = (u2 * u) >> 14;
    return u - ((u3 * 2427) >> 14);
}

AUDIO_CODE void acid_render(int32_t *out)
{
    acid_t *v = &acid;
    const acid_params_t *P = &acid_p;
    uint32_t oct, inc_new, i;
    int32_t amp_new, g_new, cp, dinc, dg, damp;
    const int16_t *tbl;

    /* ---------------- control tick ---------------------------------- */
    if (v->slide_left) {                      /* linear in pitch, exactly n ticks */
        v->pitch_q16 += v->slide_step;
        v->pitch = v->pitch_q16 >> 16;
        if (--v->slide_left == 0) v->pitch = v->target;
    }
    inc_new = pitch_inc(v->pitch + ((int32_t)P->tune << 6), &oct);
    if (oct > WT_MIPS - 1) oct = WT_MIPS - 1;
    tbl = wt[P->wave & 3][oct];

    v->env = (int32_t)(((uint32_t)v->env * v->env_coef) >> 16);
    v->acc_env = (v->acc_env * ACC_PULSE_COEF) >> 16;
    if (v->acc_env > v->acc_cap) v->acc_cap += ((v->acc_env - v->acc_cap) * ACC_UP) >> 16;
    else v->acc_cap += ((v->acc_env - v->acc_cap) * ACC_DOWN) >> 16;

    switch (v->stage) {
    case ST_ATTACK:
        amp_new = v->amp + (((v->peak - v->amp) * VCA_ATTACK) >> 16);
        if (v->peak - amp_new < 64) { amp_new = v->peak; v->stage = ST_DECAY; }
        break;
    case ST_DECAY:
        amp_new = (v->amp * (int32_t)vca_coef[P->decay & 127]) >> 16;
        break;
    case ST_RELEASE:
        amp_new = (v->amp * VCA_RELEASE) >> 16;
        if (amp_new < 16) { amp_new = 0; v->stage = ST_IDLE; }
        break;
    default:
        amp_new = 0;
        break;
    }

    /* cutoff, in 1/64 semitone above C1 */
    if (P->keytrack == CUT_KEY)
        cp = v->pitch + ((int32_t)P->tune << 6) - 660 + (int32_t)(P->cutoff & 127) * 25;
    else
        cp = 470 + (int32_t)(P->cutoff & 127) * 42;
    cp += (v->env * ((int32_t)(P->envmod & 127) * 30)) >> 16;
    cp += (v->acc_cap * ((int32_t)(P->accent & 127) * 40)) >> 16;
    cp += v->charge * 6;
    cp += (int32_t)evo_cut_ofs << 7;
    v->dbg_cut = cp;
    {
        int32_t idx = (cp >> 4) + 48;
        if (idx < 0) idx = 0;
        if (idx > 511) idx = 511;
        g_new = cut_g[idx];
    }
    v->k = res_k[P->res & 127];

    if (!v->active) {                         /* silent: keep coefficients fresh */
        v->inc = inc_new;
        v->g = g_new;
        return;
    }

    dinc = ((int32_t)inc_new - (int32_t)v->inc) >> 5;
    dg = (g_new - v->g) >> 5;
    damp = (amp_new - v->amp) >> 5;

    /* ---------------- audio --------------------------------------------- */
    {
        uint32_t ph = v->phase, inc = v->inc;
        int32_t g = v->g, k = v->k, amp = v->amp;
        int32_t y1 = v->y1, y2 = v->y2, y3 = v->y3, y4 = v->y4;
        int32_t dcx = v->dcx, dcy = v->dcy;
        int32_t kc = k >> 1;                        /* pass-band compensation */
        uint32_t mode = P->drive;
        int32_t gain = 256 + (int32_t)(P->drive_amt & 127) * 14;   /* Q8, 1..8 */
        int32_t level = P->level & 127;

        for (i = 0; i < CTRL_LEN; i++) {
            int32_t a, b, s, u, x, os;
            uint32_t idx = ph >> 22;
            a = tbl[idx];
            b = tbl[idx + 1];
            s = a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFF)) >> 15);
            s >>= 1;                                 /* Q15 -> Q14 */
            ph += inc;
            inc += (uint32_t)dinc;

            x = 0;
            for (os = 0; os < 2; os++) {
                u = sat_cubic(s - ((k * y4) >> 12));
                y1 += ((u - y1) * g) >> 16;
                y2 += ((y1 - y2) * g) >> 16;
                y3 += ((y2 - y3) * g) >> 16;
                y4 += ((y3 - y4) * g) >> 16;
                x += y4;
            }
            x >>= 1;
            x += (x * kc) >> 12;
            g += dg;

            x = (x * amp) >> 15;                     /* VCA */
            amp += damp;

            switch (mode) {
            case DRV_SOFT:
                x = sat_cubic((x * gain) >> 8);
                break;
            case DRV_HARD:
                x = ((x * gain) >> 8) + 1300;
                if (x > Q14_ONE) x = Q14_ONE;
                if (x < -Q14_ONE) x = -Q14_ONE;
                break;
            case DRV_FOLD:
                x = ((x * gain) >> 8) + 3200;
                if (x > 5 * Q14_ONE) x = 5 * Q14_ONE;
                if (x < -5 * Q14_ONE) x = -5 * Q14_ONE;
                while (x > Q14_ONE || x < -Q14_ONE) {
                    if (x > Q14_ONE) x = 2 * Q14_ONE - x;
                    else x = -2 * Q14_ONE - x;
                }
                break;
            default:
                break;
            }
            /* DC blocker (~26 Hz) */
            {
                int32_t yv = x - dcx + ((dcy * 32604) >> 15);
                dcx = x;
                dcy = yv;
                x = yv;
            }
            out[i] += (x * level) >> 7;
        }
        v->phase = ph;
        v->y1 = y1; v->y2 = y2; v->y3 = y3; v->y4 = y4;
        v->dcx = dcx; v->dcy = dcy;
    }
    v->inc = inc_new;
    v->g = g_new;
    v->amp = amp_new;
    if (v->stage == ST_IDLE) {
        v->active = 0;
        v->amp = 0;
    }
}
