/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * CPL, the Playdate-only layer (see cpl.h).  Update loop only, except the two
 * link-time wraps at the end (the SPEAKER stage runs in the audio callback,
 * CRANK BITE in the update loop where CORROSION runs).
 */
#include <stdio.h>
#include <string.h>
#include "cpl.h"
#include "app_glue.h"
#include "corrosion.h"
#include "crank.h"
#include "pump.h"
#include "storage.h"
#include "ui.h"
#include "layers.h"

cpl_project_t cplp;
cpl_settings_t cpl_set = { SPK_AUTO, 0, 0 };
acid_params_t base_acid;
fx_params_t base_fx;
volatile uint8_t cpl_mic_level;
uint8_t cpl_mic_wanted, cpl_tilt_wanted;
const char *cpl_ext_path = "etch_crank_ext.sav";
const char *cpl_settings_path = "etch_crank_settings.bin";

const char *const mod_src_names[MS_COUNT] = { "OFF", "CRANK", "TILT", "LFO", "ENV", "MIC" };
const char *const mod_dst_names[MD_COUNT] = { "ENV MOD", "DECAY", "ACCENT", "DRV AMT", "RESO", "CUTOFF",
                                              "DLY MIX", "DLY FB", "COMB MIX", "CRUSH", "LEVEL" };

static uint16_t rng = 0xACE1;
static uint8_t rnd8(void)
{
    rng ^= (uint16_t)(rng << 7);
    rng ^= (uint16_t)(rng >> 9);
    rng ^= (uint16_t)(rng << 8);
    return (uint8_t)rng;
}

void cpl_sync_base(void)
{
    base_acid = acid_p;
    base_fx = fx_p;
}

static void bind_layers(void)
{
    layers_bind(cplp.layers, sizeof cplp.layers);
    layers_knob[EK_ENVMOD] = &base_acid.envmod;
    layers_knob[EK_DECAY] = &base_acid.decay;
    layers_knob[EK_ACCENT] = &base_acid.accent;
    layers_knob[EK_DRVAMT] = &base_acid.drive_amt;
    layers_knob[EK_RUSTRES] = &LD->rust.res;
    layers_knob[EK_RUSTCUT] = &LD->rust.cutoff;
    layers_knob[EK_DLYMIX] = &base_fx.dly_mix;
    layers_knob[EK_COMBMIX] = &base_fx.comb_mix;
}

void cpl_new_project(void)
{
    memset(&cplp, 0, sizeof cplp);
    cplp.lfo_rate = 6;
    cplp.mgroup[0] = 0x06;        /* KICK + SNARE */
    cplp.mgroup[1] = 0x18;        /* the hats     */
    bind_layers();
}

void cpl_init(void)
{
    cpl_new_project();
    cpl_sync_base();
    layers_init();
}

void cpl_before_save(void)
{
    acid_p = base_acid;
    acid_p.cutoff = crank_ui.stored_cut;
    acid_p.res = crank_ui.stored_res;
    base_fx.bpm = SH->proj.synth.bpm;
    fx_p = base_fx;
}

/* ---- the expansion seed --------------------------------------------------------- */
const char *cpl_ext_code(void)
{
    static char b[5];
    if (!cplp.has_ext) return "";
    memcpy(b, cplp.ext, 4);
    b[4] = 0;
    return b;
}

uint16_t cpl_ext_seed(void)
{
    uint16_t v = 0;
    int i;
    for (i = 0; i < 4 && cplp.has_ext; i++) {
        char c = cplp.ext[i];
        v = (uint16_t)(v << 4 | (c >= 'A' ? c - 'A' + 10 : c - '0'));
    }
    return v;
}

/* ---- motion lanes ------------------------------------------------------------------ */
static uint8_t rec_state, rec_last_step = 0xFF, rec_pat;
static uint32_t rec_step_ms;
static int16_t rec_buf[LANE_PTS];
static uint8_t rec_last_pos;

void cpl_rec_arm(void)
{
    if (rec_state) { rec_state = 0; ui_message("CRANK REC OFF"); return; }
    if (!SH->playing) { ui_message("PLAY FIRST: CRANK REC STARTS ON THE NEXT BAR"); return; }
    rec_state = 1;
    ui_message("CRANK REC ARMED: THE NEXT BAR IS RECORDED");
}

uint8_t cpl_rec_state(void) { return rec_state; }

uint8_t cpl_motion_playing(void)
{
    return (uint8_t)((cplp.lane_on >> (SH->cur & 31)) & 1);
}

void cpl_lane_clear(uint8_t pat)
{
    cplp.lane_on &= ~(1u << (pat & 31));
    memset(cplp.lane[pat & 31], 0, sizeof cplp.lane[0]);
    memset(cplp.anchor[pat & 31], 0, sizeof cplp.anchor[0]);
}

static void rec_frame(uint32_t now_ms)
{
    uint8_t st = SH->step, pos, i;
    uint32_t step_ms = 15000u / (SH->proj.synth.bpm ? SH->proj.synth.bpm : 120);
    if (!rec_state) return;
    if (!SH->playing) { rec_state = 0; return; }
    if (st != rec_last_step) {
        uint8_t wrapped = rec_last_step != 0xFF && st < rec_last_step;
        rec_last_step = st;
        rec_step_ms = now_ms;
        if (wrapped && rec_state == 1) {                      /* the bar line: go */
            rec_state = 2;
            rec_pat = SH->cur;
            rec_last_pos = 0;
            for (i = 0; i < LANE_PTS; i++) rec_buf[i] = 0;
        } else if (wrapped && rec_state == 2) {               /* one pass: done */
            int32_t ofs;
            memcpy(cplp.lane[rec_pat], rec_buf, sizeof rec_buf);
            memcpy(cplp.anchor[rec_pat], rec_buf, sizeof rec_buf);
            cplp.lane_on |= 1u << rec_pat;
            rec_state = 0;
            /* the live offset is now in the lane: the crank starts afresh on top */
            ofs = crank_cut_offset_q8();
            crank_ui.cut_q8 -= ofs;
            ui_message("CRANK LANE RECORDED: PATTERN %02u", rec_pat + 1);
            return;
        }
    }
    if (rec_state == 2) {
        uint32_t into = now_ms - rec_step_ms;
        uint8_t q = (uint8_t)(into * 4 / (step_ms ? step_ms : 1));
        int16_t v = (int16_t)crank_cut_offset_q8();
        if (q > 3) q = 3;
        pos = (uint8_t)((st & 15) * 4 + q);
        /* fill everything since the last frame with this value */
        for (i = rec_last_pos; i != (uint8_t)((pos + 1) & 63); i = (uint8_t)((i + 1) & 63)) rec_buf[i] = v;
        rec_last_pos = pos;
    }
}

/* CORRODE treats a lane as material: shift, thin, re-time; DRIFT nudges it and
 * pulls it back toward its anchor (the recording).  Own random stream. */
void cpl_lanes_evolve(uint8_t mode)
{
    uint8_t p = SH->cur & 31, i;
    int16_t *l = cplp.lane[p], *a = cplp.anchor[p], t[LANE_PTS];
    if (!((cplp.lane_on >> p) & 1) || mode == EVO_STABLE) return;
    rng ^= (uint16_t)(ui_entropy | 1);
    {   /* shift by 1 (DRIFT) or 1..4 (CORRODE) positions */
        int k = (mode == EVO_CORRODE ? 1 + (rnd8() & 3) : 1) * ((rnd8() & 1) ? 1 : -1);
        for (i = 0; i < LANE_PTS; i++) t[i] = l[(i - k + LANE_PTS) & 63];
        memcpy(l, t, sizeof t);
    }
    if (mode == EVO_CORRODE) {
        if (rnd8() < 96)                                    /* thin */
            for (i = 0; i < LANE_PTS; i++) l[i] = (int16_t)(l[i] * 7 / 8);
        if (rnd8() < 64) {                                  /* re-time: swap two beats */
            uint8_t b1 = rnd8() & 3, b2 = (uint8_t)((b1 + 1 + (rnd8() % 3)) & 3);
            memcpy(t, &l[b1 * 16], 32);
            memmove(&l[b1 * 16], &l[b2 * 16], 32);
            memcpy(&l[b2 * 16], t, 32);
        }
        for (i = 0; i < LANE_PTS; i++) l[i] = (int16_t)((l[i] * 7 + a[i]) / 8);   /* bounded */
    } else {
        for (i = 0; i < LANE_PTS; i++) l[i] = (int16_t)((l[i] * 3 + a[i]) / 4);   /* pulled back */
    }
}

/* ---- scenes -------------------------------------------------------------------------- */
static scene_t morph_from;
static int morph_to = -1;
static int32_t morph_deg;                  /* crank degrees (Q8) into the morph: 0 .. 180 */

static void apply_sound(const acid_params_t *a, const fx_params_t *f, uint8_t mutes, uint8_t with_mutes)
{
    uint8_t bpm = base_fx.bpm;
    base_acid = *a;
    base_fx = *f;
    base_fx.bpm = bpm;
    crank_set_stored(a->cutoff, a->res);
    if (with_mutes && mutes != SH->proj.mutes) app_send(CMD(CMD_MUTES, mutes));
}

void cpl_scene_store(int i)
{
    scene_t *s = &cplp.scene[i & 3];
    s->used = 1;
    s->acid = base_acid;
    s->acid.cutoff = crank_ui.stored_cut;
    s->acid.res = crank_ui.stored_res;
    s->fx = base_fx;
    s->mutes = SH->proj.mutes;
    ui_message("SCENE %d STORED", (i & 3) + 1);
}

void cpl_scene_recall(int i)
{
    scene_t *s = &cplp.scene[i & 3];
    if (!s->used) return;
    apply_sound(&s->acid, &s->fx, s->mutes, 1);
    ui_message("SCENE %d", (i & 3) + 1);
}

void cpl_scene_clear(int i) { cplp.scene[i & 3].used = 0; ui_message("SCENE %d CLEARED", (i & 3) + 1); }

void cpl_morph_begin(int i)
{
    if (!cplp.scene[i & 3].used) { morph_to = -1; return; }
    morph_to = i & 3;
    morph_deg = 0;
    morph_from.acid = base_acid;
    morph_from.acid.cutoff = crank_ui.stored_cut;
    morph_from.acid.res = crank_ui.stored_res;
    morph_from.fx = base_fx;
}

static uint8_t mix8(uint8_t a, uint8_t b, int t) { return (uint8_t)(a + ((int)b - a) * t / 127); }

void cpl_morph_by(int32_t delta_q8)
{
    const scene_t *to;
    acid_params_t a;
    fx_params_t f;
    int t;
    if (morph_to < 0) return;
    to = &cplp.scene[morph_to];
    /* the whole morph in 180 degrees of crank */
    morph_deg += delta_q8;
    if (morph_deg < 0) morph_deg = 0;
    if (morph_deg > 180 * 256) morph_deg = 180 * 256;
    t = (int)(morph_deg * 127 / (180 * 256));
    a = morph_from.acid;
    f = morph_from.fx;
    a.cutoff = mix8(a.cutoff, to->acid.cutoff, t); a.res = mix8(a.res, to->acid.res, t);
    a.envmod = mix8(a.envmod, to->acid.envmod, t); a.decay = mix8(a.decay, to->acid.decay, t);
    a.accent = mix8(a.accent, to->acid.accent, t); a.slide = mix8(a.slide, to->acid.slide, t);
    a.drive_amt = mix8(a.drive_amt, to->acid.drive_amt, t); a.level = mix8(a.level, to->acid.level, t);
    if (t >= 64) { a.wave = to->acid.wave; a.drive = to->acid.drive; a.keytrack = to->acid.keytrack; }
    f.drive = mix8(f.drive, to->fx.drive, t); f.dly_fb = mix8(f.dly_fb, to->fx.dly_fb, t);
    f.dly_mix = mix8(f.dly_mix, to->fx.dly_mix, t); f.dly_rot = mix8(f.dly_rot, to->fx.dly_rot, t);
    f.comb_fb = mix8(f.comb_fb, to->fx.comb_fb, t); f.comb_mix = mix8(f.comb_mix, to->fx.comb_mix, t);
    f.crush_bits = mix8(f.crush_bits, to->fx.crush_bits, t);
    f.crush_hold = mix8(f.crush_hold, to->fx.crush_hold, t);
    if (t >= 64) { f.dly_steps = to->fx.dly_steps; f.comb_note = to->fx.comb_note; }
    apply_sound(&a, &f, 0, 0);
}

uint8_t cpl_morph_amount(void) { return morph_to < 0 ? 0 : (uint8_t)(morph_deg * 127 / (180 * 256)); }

/* ---- mute groups ------------------------------------------------------------------------ */
void cpl_mgroup_toggle(int g)
{
    uint8_t m = cplp.mgroup[g & 1], mutes = SH->proj.mutes;
    if (!m) return;
    if ((mutes & m) == m) mutes = (uint8_t)(mutes & ~m);
    else mutes |= m;
    app_send(CMD(CMD_MUTES, mutes));
}

uint8_t cpl_mgroup_of(uint8_t bit)
{
    if (cplp.mgroup[0] & bit) return 1;
    if (cplp.mgroup[1] & bit) return 2;
    return 0;
}

void cpl_mgroup_assign(uint8_t bit, uint8_t g)
{
    cplp.mgroup[0] &= (uint8_t)~bit;
    cplp.mgroup[1] &= (uint8_t)~bit;
    if (g == 1) cplp.mgroup[0] |= bit;
    if (g == 2) cplp.mgroup[1] |= bit;
}

/* ---- modulation ------------------------------------------------------------------------- */
static uint32_t lfo_phase;                  /* 16.16 */
static float tilt_now;

static int src_value(uint8_t src)          /* -127 .. 127 */
{
    switch (src) {
    case MS_CRANK: { int32_t o = crank_cut_offset_q8() >> 8; return o < -127 ? -127 : o > 127 ? 127 : o; }
    case MS_TILT: { int v = (int)(tilt_now * 254.0f); return v < -127 ? -127 : v > 127 ? 127 : v; }
    case MS_LFO: {
        uint32_t ph = lfo_phase >> 8 & 255;                   /* 0..255 a cycle */
        switch (cplp.lfo_shape & 3) {
        case LFO_TRI: return ph < 128 ? (int)ph * 2 - 127 : 383 - (int)ph * 2;
        case LFO_SQR: return ph < 128 ? 127 : -127;
        case LFO_SAW: return (int)ph - 128;
        default: { static int sh; static uint32_t last; if ((lfo_phase >> 16) != last) { last = lfo_phase >> 16; sh = (int)rnd8() - 128; } return sh; }
        }
    }
    case MS_ENV: return (int)(acid.env >> 9);                  /* Q16 -> 0..127 */
    case MS_MIC: return cpl_mic_level;
    default: return 0;
    }
}

static uint8_t clamp127(int v) { return (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v); }

static void apply_mods(void)
{
    int sum[MD_COUNT] = { 0 }, i, any = 0;
    acid_params_t a = base_acid;
    fx_params_t f = base_fx;
    cpl_mic_wanted = cpl_tilt_wanted = 0;
    for (i = 0; i < CPL_MODS; i++) {
        const modrow_t *m = &cplp.mod[i];
        if (m->src == MS_OFF || m->dst >= MD_COUNT || !m->depth) continue;
        sum[m->dst] += src_value(m->src) * m->depth / 127;
        any = 1;
        if (m->src == MS_MIC) cpl_mic_wanted = 1;
        if (m->src == MS_TILT) cpl_tilt_wanted = 1;
    }
    if (cpl_set.tilt) {                                         /* TILT -> delay send */
        cpl_tilt_wanted = 1;
        sum[MD_DLYMIX] += (int)(tilt_now * 160.0f);
        any = 1;
    }
    crank_mod_cut_q8 = sum[MD_CUTOFF] * 256;
    crank_mod_res_q8 = sum[MD_RES] * 256;
    if (any) {
        a.envmod = clamp127(a.envmod + sum[MD_ENVMOD]);
        a.decay = clamp127(a.decay + sum[MD_DECAY]);
        a.accent = clamp127(a.accent + sum[MD_ACCENT]);
        a.drive_amt = clamp127(a.drive_amt + sum[MD_DRVAMT]);
        a.level = clamp127(a.level + sum[MD_LEVEL]);
        f.dly_mix = clamp127(f.dly_mix + sum[MD_DLYMIX]);
        f.dly_fb = clamp127(f.dly_fb + sum[MD_DLYFB]);
        f.comb_mix = clamp127(f.comb_mix + sum[MD_COMBMIX]);
        { int b = f.crush_bits - sum[MD_CRUSH] / 10; f.crush_bits = (uint8_t)(b < 4 ? 4 : b > 16 ? 16 : b); }
    }
    /* the engine's values: base (+ modulation).  Cutoff and resonance belong
     * to the crank's path; tempo to the project */
    a.cutoff = acid_p.cutoff;
    a.res = acid_p.res;
    f.bpm = SH->proj.synth.bpm;
    if (memcmp(&a, (const void *)&acid_p, sizeof a)) acid_p = a;
    if (memcmp(&f, (const void *)&fx_p, sizeof f)) fx_p = f;
}

/* ---- once a frame ------------------------------------------------------------------------ */
static uint8_t headphones_now = 1, speaker_auto;
static uint8_t last_commits;
static uint32_t last_ms;
static uint16_t last_quarter;

void cpl_frame(uint32_t now_ms, float tilt_x, int headphones)
{
    uint32_t dt = last_ms ? now_ms - last_ms : 20;
    uint16_t quarter;
    last_ms = now_ms;
    tilt_now = tilt_x;
    headphones_now = (uint8_t)headphones;
    speaker_auto = (uint8_t)!headphones;
    /* the LFO: ETCH's rate 0..15 -> 0.1 .. 12 Hz */
    {
        static const uint16_t hz100[16] = { 10, 15, 25, 40, 60, 80, 100, 130, 170, 220, 280, 360, 460, 600, 800, 1200 };
        lfo_phase += (uint32_t)hz100[cplp.lfo_rate & 15] * 65536u / 100u * dt / 1000u;
    }
    layers_has_ext = cplp.has_ext;
    layers_ext_seed = cpl_ext_seed();
    rec_frame(now_ms);
    if (SH->evo_commits != last_commits) {
        last_commits = SH->evo_commits;
        cpl_lanes_evolve(SH->proj.gen.mode);
    }
    /* the lane the audio plays, and the quarter-step length in control ticks */
    crank_lane = (cpl_motion_playing() && rec_state != 2) ? cplp.lane[SH->cur & 31] : NULL;
    if (crank_lane && !crank_ui.engaged) crank_ui.sent_cut = -1;   /* engage the audio half */
    quarter = (uint16_t)(15u * 1023u / (SH->proj.synth.bpm ? SH->proj.synth.bpm : 120) / 4u);
    if (quarter != last_quarter) { last_quarter = quarter; app_send(CMD(PCMD_QUARTER, quarter)); }
    apply_mods();
}

uint8_t cpl_speaker_on(void)
{
    return cpl_set.speaker == SPK_ON || (cpl_set.speaker == SPK_AUTO && speaker_auto);
}

uint8_t cpl_bite_active(void)
{
    return cpl_set.bite && !crank_ui.docked;
}

/* ---- CRANK BITE: CORROSION's own intensity argument ------------------------------------------ */
uint8_t __real_corr_mutate(pattern_t *dst, const pattern_t *src, const pattern_t *anchor,
                           const gen_t *g, uint8_t mode, uint8_t intensity, uint16_t seed);
uint8_t __wrap_corr_mutate(pattern_t *dst, const pattern_t *src, const pattern_t *anchor,
                           const gen_t *g, uint8_t mode, uint8_t intensity, uint16_t seed)
{
    if (cpl_bite_active()) {                        /* how open the filter is: 0..15 */
        int v = ((crank_ui.cut_q8 >> 8) + 4) >> 3;
        intensity = (uint8_t)(v > 15 ? 15 : v);
    }
    return __real_corr_mutate(dst, src, anchor, g, mode, intensity, seed);
}

/* ---- SPEAKER, on the limiter (audio callback) --------------------------------------------------
 * Mono, with a gentle lift between about 120 and 400 Hz (the Playdate's speaker
 * gives little below that), then ETCH DS's limiter.  Off: untouched. */
static int32_t spk_lp1, spk_lp2;
void __real_fx_limit(int32_t *l, int32_t *r, int16_t *ol, int16_t *or_, uint32_t n);
void __wrap_fx_limit(int32_t *l, int32_t *r, int16_t *ol, int16_t *or_, uint32_t n)
{
    if (cpl_speaker_on()) {
        uint32_t i;
        for (i = 0; i < n; i++) {
            int32_t m = (l[i] + r[i]) >> 1;
            spk_lp1 += ((m - spk_lp1) * 4843) >> 16;          /* ~400 Hz */
            spk_lp2 += ((m - spk_lp2) * 1496) >> 16;          /* ~120 Hz */
            m += ((spk_lp1 - spk_lp2) * 39322) >> 16;          /* +0.6 of the band */
            l[i] = r[i] = m;
        }
    }
    __real_fx_limit(l, r, ol, or_, n);
}

/* ---- the sidecar -------------------------------------------------------------------------------- */
#define EXT_SLOT  16384u
#define EXT_HDR   16
typedef char ext_fits[(sizeof(cpl_project_t) + EXT_HDR <= EXT_SLOT) ? 1 : -1];

/* ETCH's order: invalidate, data, checksum what was written, header, magic last */
uint8_t cpl_save_slot(uint8_t slot)
{
    static uint8_t buf[sizeof(cpl_project_t)];
    uint8_t hdr[EXT_HDR];
    uint32_t off = (uint32_t)slot * EXT_SLOT, size = sizeof(cpl_project_t);
    uint16_t sum;
    FILE *f = fopen(cpl_ext_path, "r+b");
    if (!f) {                                       /* create it, full length */
        static const uint8_t z[1024];
        uint32_t k;
        f = fopen(cpl_ext_path, "w+b");
        if (!f) return 0;
        for (k = 0; k < (uint32_t)DS_SLOTS * EXT_SLOT / sizeof z; k++) fwrite(z, sizeof z, 1, f);
        fclose(f);
        f = fopen(cpl_ext_path, "r+b");
        if (!f) return 0;
    }
    memset(hdr, 0, sizeof hdr);
    fseek(f, (long)off, SEEK_SET);
    fwrite(hdr, 4, 1, f);                           /* 1: invalidate */
    fclose(f);
    f = fopen(cpl_ext_path, "r+b");
    if (!f) return 0;
    fseek(f, (long)(off + EXT_HDR), SEEK_SET);
    fwrite(&cplp, size, 1, f);                      /* 2: data */
    fclose(f);
    f = fopen(cpl_ext_path, "rb");
    if (!f) return 0;
    fseek(f, (long)(off + EXT_HDR), SEEK_SET);
    if (fread(buf, size, 1, f) != 1) { fclose(f); return 0; }
    fclose(f);
    sum = etch_fletcher16(buf, size);               /* 3: what is on flash */
    hdr[4] = 1;
    hdr[8] = (uint8_t)size; hdr[9] = (uint8_t)(size >> 8); hdr[10] = (uint8_t)(size >> 16); hdr[11] = (uint8_t)(size >> 24);
    hdr[12] = (uint8_t)sum; hdr[13] = (uint8_t)(sum >> 8);
    f = fopen(cpl_ext_path, "r+b");
    if (!f) return 0;
    fseek(f, (long)(off + 4), SEEK_SET);
    fwrite(hdr + 4, EXT_HDR - 4, 1, f);             /* 4: header */
    fclose(f);
    f = fopen(cpl_ext_path, "r+b");
    if (!f) return 0;
    fseek(f, (long)off, SEEK_SET);
    fwrite("ETCK", 4, 1, f);                        /* 5: magic last */
    fclose(f);
    return 1;
}

uint8_t cpl_load_slot(uint8_t slot)
{
    static uint8_t buf[sizeof(cpl_project_t)];
    uint8_t hdr[EXT_HDR];
    uint32_t off = (uint32_t)slot * EXT_SLOT, size;
    FILE *f = fopen(cpl_ext_path, "rb");
    if (!f) return 0;
    fseek(f, (long)off, SEEK_SET);
    if (fread(hdr, EXT_HDR, 1, f) != 1 || memcmp(hdr, "ETCK", 4)) { fclose(f); return 0; }
    size = (uint32_t)hdr[8] | (uint32_t)hdr[9] << 8 | (uint32_t)hdr[10] << 16 | (uint32_t)hdr[11] << 24;
    if (size != sizeof(cpl_project_t) || fread(buf, size, 1, f) != 1) { fclose(f); return 0; }
    fclose(f);
    if (etch_fletcher16(buf, size) != (uint16_t)(hdr[12] | hdr[13] << 8)) return 0;
    memcpy(&cplp, buf, size);
    bind_layers();
    return 1;
}

void cpl_settings_save(void)
{
    FILE *f = fopen(cpl_settings_path, "wb");
    if (!f) return;
    fwrite("ECS1", 4, 1, f);
    fwrite(&cpl_set, sizeof cpl_set, 1, f);
    fclose(f);
}

void cpl_settings_load(void)
{
    char m[4];
    cpl_settings_t s;
    FILE *f = fopen(cpl_settings_path, "rb");
    if (!f) return;
    if (fread(m, 4, 1, f) == 1 && !memcmp(m, "ECS1", 4) && fread(&s, sizeof s, 1, f) == 1) cpl_set = s;
    fclose(f);
}
