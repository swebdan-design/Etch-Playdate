/*
 * ETCH CRANK -- by Synth Centric
 * The crank's audio half (see crank.h): runs inside the audio callback, once
 * per ACID control tick, through a link-time wrap of ETCH DS's acid_render
 * (-Wl,--wrap=acid_render).  Integer multiply/shift/add only.
 */
#include "crank.h"
#include "audio.h"
#include "pump.h"
#include "app.h"

volatile int32_t crank_cut_now_q8, crank_res_now_q8;

static int32_t cut_tgt, res_tgt, cut_cur, res_cur;
static uint8_t engaged;

/* the motion lane (CPL): the update loop points this at the playing
 * pattern's 64-point lane (or NULL); it is read once per control tick.  The
 * position within the bar comes from the sequencer's step plus a count of
 * control ticks into the step, in quarters of quarter_ticks. */
const int16_t *volatile crank_lane;
static uint8_t lane_step = 0xFF, lane_q;
static uint16_t lane_tick, quarter_ticks = 30;
volatile uint8_t crank_lane_pos;

/* one pole per control tick (0.978 ms): 1 - exp(-0.978 / 4) = 0.2169 */
#define GLIDE_Q16 14214

/* pump_init: the engine's own values stand until the crank next moves */
void crank_audio_reset(void)
{
    engaged = 0;
    lane_step = 0xFF;
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
    else if (c == PCMD_QUARTER) quarter_ticks = (uint16_t)(arg ? arg : 1);
    else if (c == PCMD_RESET) engaged = 0;       /* a project load: its own values */
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
        const int16_t *lane = crank_lane;
        int32_t tgt = cut_tgt;
        if (lane) {
            uint8_t st = SH ? SH->step : 0;
            if (st != lane_step) { lane_step = st; lane_q = 0; lane_tick = 0; }
            else if (++lane_tick >= quarter_ticks && lane_q < 3) { lane_q++; lane_tick = 0; }
            crank_lane_pos = (uint8_t)(((st & 15) << 2) | lane_q);
            tgt += lane[crank_lane_pos];
            if (tgt < 0) tgt = 0;
            if (tgt > CRANK_MAX_Q8) tgt = CRANK_MAX_Q8;
        }
        cut_cur = glide(cut_cur, tgt);
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
