/*
 * ETCH CRANK -- by Synth Centric
 * The answering generator (an ETCH DS layer): RUST, METAL, NOISE and the
 * ratchets, written as answers to what CORROSION composed.  Update loop only.
 *
 * It reads CORROSION's output (the pattern's bass and drums) and the GEN
 * settings, and it uses CORROSION's pure helpers (corr_in_scale, corr_range),
 * never CORROSION's random stream: it has its own, seeded from the code's 4th
 * group (the expansion seed), the pattern and its generation.  So:
 *   - a 16-digit code gives the same layers on every machine, and evolving
 *     the pattern re-answers it the same way everywhere
 *   - CORROSION's own output (and every 12-digit code) is untouched
 *
 * The answer, per step:
 *   RUST    where the bass rests it answers (call and response), less often
 *           over it; offbeats lean in.  Notes walk the scale an octave above
 *           the bass, from the bass's own root.  Accents follow the bass's;
 *           in PARA3, and for INDUSTRIAL, some answers are 3-note stabs.
 *   METAL   fills the gaps in the closed hat, leaning on the "e" and "a"
 *   NOISE   sparse: the bar's last step, and pushes into the snare
 *   ratchets  hat rolls, the odd snare roll into the bar line, a rare kick
 *   length  ELECTRO and BROKEN sometimes run METAL against the bar (12 or 14)
 * Density, style and locks steer it; a locked layer part is never rewritten.
 */
#include <string.h>
#include "layers.h"
#include "app.h"
#include "corrosion.h"

static uint32_t ar;
static uint32_t rnd32(void)
{
    ar ^= ar << 13;
    ar ^= ar >> 17;
    ar ^= ar << 5;
    return ar;
}
static uint8_t chance(int pct) { return (int)(rnd32() % 100u) < pct; }

static uint8_t up_scale(uint8_t n, int steps, const gen_t *g)
{
    while (steps > 0 && n < 59) { n++; if (corr_in_scale(n, g->key, g->scale)) steps--; }
    while (steps < 0 && n > 12) { n--; if (corr_in_scale(n, g->key, g->scale)) steps++; }
    return n;
}

static uint32_t material_hash(const pattern_t *p)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < 56; i++) h = (h ^ b[i]) * 16777619u;     /* notes, flags, prob, drums */
    return h;
}

void layers_generate(uint8_t pat)
{
    const pattern_t *p = &SH->proj.pat[pat & 31];
    const gen_t *g = &SH->proj.gen;
    layer_pat_t *L = &LD->pat[pat & 31];
    uint8_t lo, hi, root, cur, i, d = g->density & 15, style = g->style;
    uint16_t ext = layers_ext_seed;
    if (L->lock) return;
    ar = 0x9E3779B9u ^ ((uint32_t)ext * 2654435761u) ^ ((uint32_t)(pat & 31) * 40503u) ^
         ((uint32_t)p->gen << 20) ^ ((uint32_t)g->seed << 4);
    if (!ar) ar = 0x2545F491u;
    rnd32(); rnd32();

    memset(L->note, 0, sizeof L->note);
    memset(L->flags, 0, sizeof L->flags);
    memset(L->ratchet, 0, sizeof L->ratchet);
    L->metal = L->noise = 0;
    L->rust_len = L->noise_len = 16;
    L->metal_len = 16;

    /* RUST: the answer */
    corr_range(g, &lo, &hi);
    root = (uint8_t)(g->key % 12 + 12 * (g->octave > 2 ? 2 : g->octave));
    for (i = 0; i < NUM_STEPS; i++) if ((p->flags[i] & STEP_TYPE_MASK) == STEP_NOTE) { root = p->note[i]; break; }
    cur = up_scale((uint8_t)(root + 12 > 59 ? 59 : root + 12), 0, g);
    if (!corr_in_scale(cur, g->key, g->scale)) cur = up_scale(cur, 1, g);
    for (i = 0; i < NUM_STEPS; i++) {
        uint8_t bass = (p->flags[i] & STEP_TYPE_MASK) != STEP_REST;
        int pct = bass ? 6 : 26 + d * 3;
        if (i & 1) pct += 8;
        if (style == STYLE_MINIMAL) pct /= 2;
        if (style == STYLE_SPARSE) pct = pct * 2 / 5;
        if (style == STYLE_ELECTRO && (i & 3) == 3) pct += 10;
        if (!chance(pct)) continue;
        cur = up_scale(cur, (int)(rnd32() % 5u) - 2, g);
        if (cur < root + 7) cur = up_scale(cur, 2, g);
        if (cur > 59) cur = 59;
        L->note[i] = cur;
        L->flags[i] = RF_ON;
        if ((p->flags[i] & STEP_ACCENT) || chance(15)) L->flags[i] |= RF_ACCENT;
        if ((LD->rust.mode == RM_PARA3 && chance(55)) || (style == STYLE_INDUSTRIAL && chance(30)))
            L->flags[i] |= RF_CHORD;
    }
    for (i = 0; i + 1 < NUM_STEPS; i++)
        if ((L->flags[i] & RF_ON) && (L->flags[i + 1] & RF_ON) && chance(12)) L->flags[i] |= RF_TIE;

    /* METAL: the gaps in the closed hat */
    for (i = 0; i < NUM_STEPS; i++) {
        int pct = ((p->drum[LANE_CH] >> i) & 1) ? 0 : ((i & 3) == 1 || (i & 3) == 3 ? 22 + d * 2 : 6 + d);
        if (style == STYLE_INDUSTRIAL) pct = pct * 3 / 2;
        if (style == STYLE_MINIMAL || style == STYLE_SPARSE) pct /= 2;
        if (chance(pct)) L->metal |= (uint16_t)(1u << i);
    }
    if ((style == STYLE_ELECTRO || style == STYLE_BROKEN) && chance(30)) L->metal_len = chance(50) ? 12 : 14;

    /* NOISE: sparse pushes */
    for (i = 0; i < NUM_STEPS; i++) {
        int pct = i == 15 ? 35 : (i + 1 < NUM_STEPS && ((p->drum[LANE_SNARE] >> (i + 1)) & 1)) ? 18 : d / 3;
        if (style == STYLE_INDUSTRIAL) pct += 8;
        if (chance(pct)) L->noise |= (uint16_t)(1u << i);
    }

    /* ratchets: 2 bits a lane per step, hits - 1 */
    for (i = 0; i < NUM_STEPS; i++) {
        uint8_t r = 0;
        if (((p->drum[LANE_CH] >> i) & 1) && chance(8 + d)) r |= (uint8_t)((chance(30) ? 2 : 1) << (LANE_CH * 2));
        if (i >= 14 && ((p->drum[LANE_SNARE] >> i) & 1) && chance(15)) r |= (uint8_t)((chance(50) ? 2 : 3) << (LANE_SNARE * 2));
        if (((p->drum[LANE_KICK] >> i) & 1) && chance(3)) r |= (uint8_t)(1 << (LANE_KICK * 2));
        L->ratchet[i] = r;
    }
    L->src_hash = material_hash(p);
}

/* every pattern CORROSION has written something into */
void layers_answer_all(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PATTERNS; i++) {
        const pattern_t *p = &SH->proj.pat[i];
        if (pattern_note_count(p) || p->drum[0] || p->drum[1] || p->drum[2] || p->drum[3]) layers_generate(i);
    }
}

/* has CORROSION changed the material since it was answered? (evolution) */
uint8_t layers_stale(uint8_t pat)
{
    return LD->pat[pat & 31].src_hash != material_hash(&SH->proj.pat[pat & 31]);
}
