/*
 * ETCH CRANK -- by Synth Centric
 * The crank's audio half (see crank.h): runs inside the audio callback, once
 * per ACID control tick, through a link-time wrap of ETCH DS's acid_render
 * (-Wl,--wrap=acid_render).  Integer multiply/shift/add only.
 */
#include "crank.h"
#include "audio.h"
#include "pump.h"

volatile int32_t crank_cut_now_q8, crank_res_now_q8;

static int32_t cut_tgt, res_tgt, cut_cur, res_cur;
static uint8_t engaged;

/* one pole per control tick (0.978 ms): 1 - exp(-0.978 / 4) = 0.2169 */
#define GLIDE_Q16 14214

/* pump_init: the engine's own values stand until the crank next moves */
void crank_audio_reset(void)
{
    engaged = 0;
}

void crank_command(uint32_t cmd)
{
    uint32_t c = cmd >> 24, arg = cmd & 0xFFFFFFu;
    if (!engaged && (c == PCMD_CUTOFF || c == PCMD_RES)) {
        /* first touch: glide from what ACID is using now */
        engaged = 1;
        cut_cur = (int32_t)(acid_p.cutoff & 127) << 8;
        res_cur = (int32_t)(acid_p.res & 127) << 8;
        cut_tgt = cut_cur;
        res_tgt = res_cur;
    }
    if (c == PCMD_CUTOFF) cut_tgt = (int32_t)arg;
    else if (c == PCMD_RES) res_tgt = (int32_t)arg;
}

static inline int32_t glide(int32_t cur, int32_t tgt)
{
    int32_t d = tgt - cur;
    if (d > -16 && d < 16) return tgt;      /* within 1/16 step: arrive */
    return cur + ((d * GLIDE_Q16) >> 16);
}

void __real_acid_render(int32_t *out);

void __wrap_acid_render(int32_t *out)
{
    if (engaged) {
        cut_cur = glide(cut_cur, cut_tgt);
        res_cur = glide(res_cur, res_tgt);
        acid_p.cutoff = (uint8_t)((cut_cur + 128) >> 8);
        acid_p.res = (uint8_t)((res_cur + 128) >> 8);
        crank_cut_now_q8 = cut_cur;
        crank_res_now_q8 = res_cur;
    } else {
        crank_cut_now_q8 = (int32_t)(acid_p.cutoff & 127) << 8;
        crank_res_now_q8 = (int32_t)(acid_p.res & 127) << 8;
    }
    __real_acid_render(out);
}
