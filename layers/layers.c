/*
 * ETCH CRANK -- by Synth Centric
 * The ETCH DS layers: glue, the audio-side sequencing, sound evolution.
 * See layers.h.
 *
 * Audio callback (two link-time wraps around ETCH DS's engine):
 *   acid_step       ETCH DS's renderer calls it on the control tick of every
 *                   16th.  After ACID's own work, the layers play their step:
 *                   RUST's note or stab, METAL and NOISE hits (FOUNDRY lanes
 *                   4 and 5), each on its own length.
 *   foundry_render  once per control tick, after FOUNDRY: RUST's gate and the
 *                   ratchet repeats count down, then RUST renders into the bus.
 * Mutes use ETCH DS's mute byte: bit 5 METAL and bit 6 NOISE (ETCH DS's
 * sequencer already reads them that way), bit 7 RUST.
 *
 * With the layers off (no expansion seed), every wrap only calls through:
 * the sound is ETCH DS's, bit for bit.
 */
#include <string.h>
#include "layers.h"
#include "app.h"
#include "corrosion.h"
#include "dsp_tables.h"

layers_data_t *LD;
static layers_data_t fallback;
const char *const evo_knob_names[EK_COUNT] = { "ENV MOD", "DECAY", "ACCENT", "DRV AMT", "RUST RES", "RUST CUT", "DLY MIX", "COMB MIX" };
uint8_t *layers_knob[EK_COUNT];           /* set by the platform: the edited sound */
uint8_t layers_has_ext;                   /* set by the platform each frame        */
uint16_t layers_ext_seed;

#define MUTE_METAL 0x20
#define MUTE_NOISE 0x40
#define MUTE_RUST  0x80

uint8_t layers_stale(uint8_t pat);

void layers_bind(void *mem, uint32_t size)
{
    LD = (mem && size >= sizeof(layers_data_t)) ? (layers_data_t *)mem : &fallback;
}

static void defaults(void)
{
    static const rust_params_t r = { RW_SAW, RW_SQUARE, 7, 20, 0, 0, 52, 70, 60, 50, RM_MONO, 70, 9, { 0 } };
    uint8_t i;
    memset(LD, 0, sizeof *LD);
    LD->rust = r;
    LD->drums45[0] = foundry_presets[4][0];
    LD->drums45[1] = foundry_presets[5][0];
    for (i = 0; i < NUM_PATTERNS; i++) LD->pat[i].rust_len = LD->pat[i].metal_len = LD->pat[i].noise_len = 16;
}

void layers_init(void)
{
    if (!LD) LD = &fallback;
    defaults();
    rust_init();
}

/* a project was loaded (or made): an empty sidecar gets the defaults; the
 * METAL and NOISE designs go into FOUNDRY */
void layers_project_loaded(void)
{
    uint8_t i, blank = 1;
    const uint8_t *b = (const uint8_t *)LD;
    for (i = 0; i < 64 && blank; i++) if (b[sizeof(layer_pat_t) * NUM_PATTERNS + i]) blank = 0;
    if (blank) defaults();
    for (i = 0; i < NUM_PATTERNS; i++) {
        layer_pat_t *L = &LD->pat[i];
        if (!L->rust_len || L->rust_len > 16) L->rust_len = 16;
        if (!L->metal_len || L->metal_len > 16) L->metal_len = 16;
        if (!L->noise_len || L->noise_len > 16) L->noise_len = 16;
    }
    if (LD->drums45[0].len_ms && LD->drums45[1].len_ms) {
        foundry_defs[4][0] = LD->drums45[0];
        foundry_defs[5][0] = LD->drums45[1];
        foundry_rebuild(4, 0);
        foundry_rebuild(5, 0);
    }
    rust_init();
    layers_evo_anchor();
}

uint8_t layers_active(void) { return LD && LD->enabled; }

/* ---- audio: stepping ------------------------------------------------------------------ */
static uint8_t step_now, rust_gate_ticks, rust_hold;
static uint8_t rat_left[NUM_LANES], rat_int[NUM_LANES], rat_cnt[NUM_LANES], ratcheting;
static uint8_t chord_buf[3];

static inline uint8_t pos_in(uint32_t abs_step, uint8_t len)
{
    while (abs_step >= len) abs_step -= len;     /* len 1..16: a few subtractions, no division */
    return (uint8_t)abs_step;
}

static uint8_t step_ticks(void)
{
    uint32_t t = step_len[SH->proj.synth.bpm] >> 5;      /* samples per 16th / control tick */
    return (uint8_t)(t > 255 ? 255 : t ? t : 1);
}

void __real_acid_step(void);
void __wrap_acid_step(void)
{
    const layer_pat_t *L;
    uint8_t s, p, mutes;
    uint32_t abs;
    __real_acid_step();
    if (!LD || !LD->enabled || !SH || !SH->playing || SH->stress) return;
    s = SH->step & 15;
    p = SH->cur & 31;
    L = &LD->pat[p];
    mutes = SH->proj.mutes;
    abs = (uint32_t)(SH->bar_total & 15) * 16u + s;      /* a 16-bar cycle for the lengths */
    step_now = s;

    /* RUST */
    {
        uint8_t rp = L->rust_len >= 16 ? s : pos_in(abs, L->rust_len);
        uint8_t prev = rp ? (uint8_t)(rp - 1) : (uint8_t)(L->rust_len - 1);
        uint8_t f = L->flags[rp];
        if ((f & RF_ON) && !(mutes & MUTE_RUST)) {
            uint8_t n = L->note[rp], cnt = 1;
            chord_buf[0] = n;
            if ((f & RF_CHORD) && rust_p.mode == RM_PARA3) {
                const gen_t *g = &SH->proj.gen;
                chord_buf[1] = (uint8_t)(corr_in_scale((uint8_t)(n + 3), g->key, g->scale) ? n + 3 : n + 4);
                chord_buf[2] = (uint8_t)(n + 7);
                cnt = 3;
            }
            rust_note(chord_buf, cnt, (uint8_t)((f & RF_ACCENT) != 0), (uint8_t)((L->flags[prev] & RF_TIE) != 0));
            rust_hold = (f & RF_TIE) != 0;
            rust_gate_ticks = (uint8_t)((step_ticks() * (rust_p.gate ? rust_p.gate : 9)) >> 4);
            if (!rust_gate_ticks) rust_gate_ticks = 1;
        }
    }
    /* METAL and NOISE: FOUNDRY's lanes 4 and 5 */
    {
        uint8_t lanes = 0;
        uint8_t mp = L->metal_len >= 16 ? s : pos_in(abs, L->metal_len);
        uint8_t np = L->noise_len >= 16 ? s : pos_in(abs, L->noise_len);
        if (((L->metal >> mp) & 1) && !(mutes & MUTE_METAL)) lanes |= 1u << LANE_METAL;
        if (((L->noise >> np) & 1) && !(mutes & MUTE_NOISE)) lanes |= 1u << LANE_NOISE;
        if (lanes) foundry_trigger(lanes, 0);
    }
}

/* FOUNDRY just triggered these lanes (the trigger guard tells us): the ones
 * that fired on this step and have a ratchet get their repeats scheduled */
void layers_on_trigger(uint8_t lanes)
{
    uint8_t l, r, st;
    const layer_pat_t *L;
    if (ratcheting || !LD || !LD->enabled || !SH) return;
    L = &LD->pat[SH->cur & 31];
    st = step_ticks();
    for (l = 0; l < NUM_LANES; l++) {
        if (!(lanes & (1u << l))) continue;
        r = (uint8_t)((L->ratchet[SH->step & 15] >> (l * 2)) & 3);
        if (!r) { rat_left[l] = 0; continue; }
        rat_left[l] = r;
        rat_int[l] = (uint8_t)(r == 1 ? st >> 1 : r == 2 ? (st * 21) >> 6 : st >> 2);   /* 1/2, ~1/3, 1/4 */
        if (!rat_int[l]) rat_int[l] = 1;
        rat_cnt[l] = rat_int[l];
    }
}

void __real_foundry_render(int32_t *L, int32_t *R, uint32_t n);
void __wrap_foundry_render(int32_t *L, int32_t *R, uint32_t n)
{
    __real_foundry_render(L, R, n);
    if (!LD || !LD->enabled) return;
    {   /* the ratchet repeats */
        uint8_t l, lanes = 0;
        for (l = 0; l < NUM_LANES; l++) {
            if (!rat_left[l]) continue;
            if (--rat_cnt[l] == 0) {
                lanes |= (uint8_t)(1u << l);
                rat_left[l]--;
                rat_cnt[l] = rat_int[l];
            }
        }
        if (lanes) {
            const pattern_t *p = &SH->proj.pat[SH->cur & 31];
            uint8_t kits = (uint8_t)((p->kit[0] & 3) | ((p->kit[1] & 3) << 2) | ((p->kit[2] & 3) << 4) | ((p->kit[3] & 3) << 6));
            ratcheting = 1;
            foundry_trigger(lanes, kits);
            ratcheting = 0;
        }
    }
    if (rust_gate_ticks && !rust_hold && --rust_gate_ticks == 0) rust_gate_off();
    if (!SH->playing && rust_busy()) rust_gate_off();
    rust_render(L, R, n);
    (void)step_now;
}

/* ---- update loop -------------------------------------------------------------------------- */
static uint8_t last_commits, was_ext, evo_written[EK_COUNT];
static uint16_t erng = 0x7A3B;
static uint8_t er8(void)
{
    erng ^= (uint16_t)(erng << 7);
    erng ^= (uint16_t)(erng >> 9);
    erng ^= (uint16_t)(erng << 8);
    return (uint8_t)erng;
}

void layers_evo_anchor(void)
{
    uint8_t k;
    for (k = 0; k < EK_COUNT; k++)
        if (layers_knob[k]) { LD->evo_anchor[k] = *layers_knob[k]; evo_written[k] = *layers_knob[k]; }
}

/* sound evolution: after a commit, each free knob takes a bounded step
 * around its anchor (DRIFT +-2 within +-8, CORRODE +-5 within +-20) */
static void sound_evolve(uint8_t mode)
{
    uint8_t k;
    int step = mode == EVO_CORRODE ? 5 : 2, bound = mode == EVO_CORRODE ? 20 : 8;
    for (k = 0; k < EK_COUNT; k++) {
        int v, a;
        if (!layers_knob[k] || ((LD->evo_lock >> k) & 1)) continue;
        if (*layers_knob[k] != evo_written[k]) LD->evo_anchor[k] = *layers_knob[k];   /* edited by hand: new home */
        a = LD->evo_anchor[k];
        v = *layers_knob[k] + (int)(er8() % (unsigned)(2 * step + 1)) - step;
        if (v > a + bound) v = a + bound;
        if (v < a - bound) v = a - bound;
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        *layers_knob[k] = (uint8_t)v;
        evo_written[k] = (uint8_t)v;
    }
}

void layers_frame(void)
{
    uint8_t c;
    if (!LD) return;
    /* the layers play when the code has its 4th group */
    if (layers_has_ext && !was_ext) { LD->enabled = 1; layers_answer_all(); }
    if (!layers_has_ext) LD->enabled = 0;
    was_ext = layers_has_ext;
    if (LD->enabled) {
        uint8_t nxt = app_next_bar_pattern();
        if (layers_stale(SH->cur)) layers_generate(SH->cur);       /* re-answer after evolution */
        if (nxt != SH->cur && layers_stale(nxt)) layers_generate(nxt);
    }
    c = SH->evo_commits;
    if (c != last_commits) {
        last_commits = c;
        if (LD->evo_on && SH->proj.gen.mode != EVO_STABLE) {
            erng ^= (uint16_t)(ui_entropy | 1);
            sound_evolve(SH->proj.gen.mode);
        }
    }
    rust_p = LD->rust;
}
