/*
 * ETCH CRANK -- by Synth Centric
 * The crank's update-loop half: gestures -> live cutoff / resonance targets.
 * See crank.h.  Runs in the update loop only; may divide (not the audio path).
 */
#include "crank.h"
#include "ipc.h"
#include "pump.h"

crank_ui_t crank_ui;
static int32_t deg_rem;        /* the part of a step not yet turned */

void app_send(uint32_t cmd);

static int32_t clampq(int32_t v)
{
    if (v < 0) return 0;
    if (v > CRANK_MAX_Q8) return CRANK_MAX_Q8;
    return v;
}

void crank_ui_init(uint8_t stored_cut, uint8_t stored_res, uint8_t docked)
{
    crank_ui_t *c = &crank_ui;
    c->stored_cut = stored_cut & 127;
    c->stored_res = stored_res & 127;
    c->cut_q8 = (int32_t)c->stored_cut << 8;
    c->res_q8 = (int32_t)c->stored_res << 8;
    c->sent_cut = c->cut_q8;
    c->sent_res = c->res_q8;
    c->docked = docked;
    c->b_held = 0;
    c->returning = 0;
    c->engaged = 0;
    deg_rem = 0;
}

/* degrees (Q8) -> value steps (Q8): 127 steps over CRANK_TRAVEL_DEG.  The
 * remainder is carried, so turning 540 degrees in any number of pieces moves
 * exactly the full 127 steps */
static int32_t deg_to_q8(int32_t delta_q8)
{
    int32_t num = delta_q8 * 127 + deg_rem;
    int32_t d = num / CRANK_TRAVEL_DEG;
    deg_rem = num - d * CRANK_TRAVEL_DEG;
    return d;
}

void crank_ui_frame(int32_t delta_q8, uint8_t docked, uint8_t b_held,
                    uint32_t now_ms, uint8_t bpm)
{
    crank_ui_t *c = &crank_ui;
    int32_t home_cut, home_res;

    /* docking: glide home over one beat, or (B held) keep the live values */
    if (docked && !c->docked) {
        if (b_held) {
            c->stored_cut = (uint8_t)((c->cut_q8 + 128) >> 8);
            c->stored_res = (uint8_t)((c->res_q8 + 128) >> 8);
            c->cut_q8 = (int32_t)c->stored_cut << 8;
            c->res_q8 = (int32_t)c->stored_res << 8;
            c->returning = 0;
        } else {
            c->returning = 1;
            c->ret_t0_ms = now_ms;
            c->ret_len_ms = 60000u / (bpm ? bpm : 120);
            c->ret_cut0 = c->cut_q8;
            c->ret_res0 = c->res_q8;
        }
    }
    if (!docked) {
        c->returning = 0;                   /* picked up again: from here */
        if (delta_q8) {
            int32_t d = deg_to_q8(delta_q8), *v = b_held ? &c->res_q8 : &c->cut_q8;
            *v = clampq(*v + d);
            if (*v == 0 || *v == CRANK_MAX_Q8) deg_rem = 0;   /* the stop absorbs the rest */
        }
    }
    if (c->returning) {
        uint32_t t = now_ms - c->ret_t0_ms;
        home_cut = (int32_t)c->stored_cut << 8;
        home_res = (int32_t)c->stored_res << 8;
        if (t >= c->ret_len_ms) {
            c->cut_q8 = home_cut;
            c->res_q8 = home_res;
            c->returning = 0;
        } else {                            /* linear in octaves */
            c->cut_q8 = c->ret_cut0 + (int32_t)(((int64_t)(home_cut - c->ret_cut0) * t) / c->ret_len_ms);
            c->res_q8 = c->ret_res0 + (int32_t)(((int64_t)(home_res - c->ret_res0) * t) / c->ret_len_ms);
        }
    }
    c->docked = docked;
    c->b_held = b_held;

    /* targets to the audio callback: only on change, and never before the
     * crank has first moved something (until then the engine's own values
     * stand untouched, exactly as on ETCH DS) */
    if (c->cut_q8 != c->sent_cut || c->res_q8 != c->sent_res) {
        c->engaged = 1;
        if (c->cut_q8 != c->sent_cut) app_send(CMD(PCMD_CUTOFF, (uint32_t)c->cut_q8));
        if (c->res_q8 != c->sent_res) app_send(CMD(PCMD_RES, (uint32_t)c->res_q8));
        c->sent_cut = c->cut_q8;
        c->sent_res = c->res_q8;
    }
}
