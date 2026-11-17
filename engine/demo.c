/*
 * ETCH DS -- by Synth Centric
 * The boot song: ETCH's demo song 1, CORRODE, loaded from its seed code.
 *
 * The groove is composed by CORROSION (unmodified) from the 12-digit code
 * 0F1C-9009-5870, exactly as ETCH's GEN ALL does on an empty, unlocked
 * pattern, so it is the same bass line and drums ETCH makes from that code.
 * The sound settings (SAW, cutoff 7, resonance 5, ENV 10, DEC 7, ACC 10,
 * SOFT drive, slide 5, 124 BPM, swing 1) and the evolution settings
 * (MUTATE 5, DRIFT) are CORRODE's from ETCH's ROM track list.
 */
#include <string.h>
#include "corrosion.h"
#include "app.h"
#include "audio.h"

void demo_load(project_t *p)
{
    static const synth_t corrode_synth = {
        WAVE_SAW, 7, 5, 10, 7, 10, DRIVE_SOFT, 5,
        0, 0, LFO_OFF, LFO_TRI,
        124, 1, 12, 1
    };
    static const char name[PROJECT_NAME_LEN] = { 'C','O','R','R','O','D','E',' ',' ',' ' };
    pattern_t pat;
    uint8_t i;

    memset(p, 0, sizeof *p);
    memcpy(p->name, name, PROJECT_NAME_LEN);
    p->synth = corrode_synth;
    corr_code_decode(&p->gen, DEMO_CODE);
    p->gen.mutation = 5;
    p->gen.mode = EVO_DRIFT;
    for (i = 0; i < CHAIN_LEN; i++) { p->chain.pat[i] = 0xFF; p->chain.rep[i] = 1; }
    p->chain.pat[0] = 0;
    p->chain.rep[0] = 4;
    p->chain.song = 0;
    for (i = 0; i < NUM_PATTERNS; i++) pattern_clear(&p->pat[i]);
    project_drum_defaults(p);

    pattern_clear(&pat);                       /* GEN ALL on an empty pattern */
    corr_generate(&pat, &p->gen, GEN_ALL);
    memcpy(&p->pat[0], &pat, sizeof pat);
}

/* ETCH's coarse synth settings -> ACID's 7-bit parameters */
void demo_import_sound(const project_t *p)
{
    const synth_t *s = &p->synth;
    static const uint8_t res_map[8] = { 0, 34, 52, 66, 80, 92, 104, 116 };
    acid_p.wave = s->wave & 3;
    acid_p.cutoff = (uint8_t)((s->cutoff & 31) * 4 + 2);
    acid_p.res = res_map[s->res & 7];
    acid_p.envmod = (uint8_t)((s->envmod & 15) * 8 + 4);
    acid_p.decay = (uint8_t)((s->decay & 15) * 8 + 4);
    acid_p.accent = (uint8_t)((s->accent & 15) * 8 + 4);
    acid_p.slide = (uint8_t)((s->slide & 15) * 8 + 4);
    acid_p.drive = s->drive > DRV_HARD ? DRV_HARD : s->drive;
    acid_p.tune = (int8_t)((int)s->tune - 12);
    fx_p.bpm = s->bpm;
}
