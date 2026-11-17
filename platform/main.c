/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * Boot, the audio callback, the update loop.
 *
 * Boot (kEventInit): the sound engine (FOUNDRY renders its drum sounds), the
 * boot song (ETCH's CORRODE from its seed code, in DRIFT), the audio pump,
 * and playback starts at once: launch it, hear music.  The crank is never
 * needed for sound.
 *
 * Two contexts on the one core:
 *   audio callback  pump_pull: commands, ETCH's clock and step rules, ACID,
 *                   FOUNDRY, FX, resampler.  Never allocates, draws, locks,
 *                   waits or touches the filesystem.
 *   update loop     buttons and crank, CORROSION's background work (evolve.c),
 *                   the screen (only what changed).
 */
#include <stdio.h>
#include <string.h>
#include "pd_api.h"
#include "app_glue.h"
#include "audio.h"
#include "corrosion.h"
#include "pump.h"
#include "crank.h"
#include "screen.h"

static PlaydateAPI *pd;

/* ---- the clock for the CPU meter ------------------------------------------
 * Device: the Cortex-M7's cycle counter (DWT CYCCNT), raw CPU cycles.
 * Simulator: the host's monotonic clock in nanoseconds.
 * render.arm.c's stage percentages assume 131 072 ticks per block (the DS
 * bus clock); screen.c rescales them by the measured tick rate. */
#if TARGET_PLAYDATE
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define DWT_LAR    (*(volatile uint32_t *)0xE0001FB0u)
static uint32_t clk_cycles(void) { return DWT_CYCCNT; }
static void clk_init(void)
{
#if ETCH_CPU_METER
    DEMCR |= 1u << 24;                  /* TRCENA */
    DWT_LAR = 0xC5ACCE55u;              /* unlock (Cortex-M7) */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;                     /* CYCCNTENA */
    host_clock = clk_cycles;
#endif
}
#else
#include <time.h>
static uint32_t clk_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec);
}
static void clk_init(void) { host_clock = clk_ns; }
#endif

uint16_t hw_noise(void)
{
    uint32_t t = host_clock ? host_clock() : 0;
    return (uint16_t)(t ^ (t >> 16) ^ pd->system->getCurrentTimeMilliseconds());
}

/* ---- the audio callback ----------------------------------------------------- */
static volatile uint32_t cb_paused;       /* callbacks while the system menu is up */
static volatile uint8_t menu_open;

static int audio_cb(void *ctx, int16_t *left, int16_t *right, int len)
{
    uint32_t t0 = host_clock ? host_clock() : 0, dt;
    (void)ctx;
    pump_pull(left, right, (uint32_t)len);
    dt = (host_clock ? host_clock() : 0) - t0;
    pstats.cb_sum += dt;
    if (dt > pstats.cb_max) pstats.cb_max = dt;
    if (menu_open) cb_paused++;
    return 1;
}

/* ---- buttons ----------------------------------------------------------------
 * Held B takes SELECT's role (docs/CONTROLS.md).  Milestone 1 implements:
 *   crank            cutoff
 *   B + crank        resonance
 *   dock             cutoff and resonance glide home over one beat
 *   B + dock         keep the live values as the stored values
 *   B + DOWN         play / stop                        (ETCH: START)
 *   B tap            FILL on / off (latched)            (ETCH LIVE: B)
 *   B + UP           arm crank recording: milestone 2 (shows a message)
 * A B tap is a press and release of B with no other button and no crank
 * movement in between, so B + crank never toggles the fill. */
static uint8_t b_combo;                   /* B is part of a chord this press */
static uint8_t fill_on;

static void start_stop(void)
{
    if (!SH->playing) {
        uint16_t e = (uint16_t)(ui_entropy ^ hw_noise());
        evo_seed(e);                      /* the moment of the press seeds the living song */
        app_send(CMD(CMD_START, e));
    } else {
        app_send(CMD(CMD_STOP, 0));
    }
}

static void input_frame(void)
{
    PDButtons cur, pushed, released;
    float dc;
    int32_t delta_q8;
    uint8_t docked, b;

    pd->system->getButtonState(&cur, &pushed, &released);
    if (pushed) ui_entropy = (uint16_t)((ui_entropy * 31u) ^ hw_noise() ^ (uint16_t)pushed);
    b = (cur & kButtonB) ? 1 : 0;

    dc = pd->system->getCrankChange();
    docked = (uint8_t)pd->system->isCrankDocked();
    delta_q8 = (int32_t)(dc * 256.0f);
    if (delta_q8) ui_entropy = (uint16_t)(ui_entropy ^ (uint16_t)delta_q8);

    if (pushed & kButtonB) b_combo = 0;
    if (b && (pushed & (kButtonUp | kButtonDown | kButtonLeft | kButtonRight | kButtonA))) b_combo = 1;
    if (b && delta_q8) b_combo = 1;

    if (b && (pushed & kButtonDown)) start_stop();
    if (b && (pushed & kButtonUp)) screen_message("CRANK REC ARRIVES IN MILESTONE 2");
    if (b && (pushed & (kButtonLeft | kButtonRight))) screen_message("LIVE IS THE ONLY SCREEN IN MILESTONE 1");
    if ((released & kButtonB) && !b_combo) {
        fill_on = (uint8_t)!fill_on;
        app_fill_latch(fill_on);
    }
    if (b && docked && !crank_ui.docked) b_combo = 1;   /* B + dock: keep values */

    crank_ui_frame(delta_q8, docked, b, pd->system->getCurrentTimeMilliseconds(), SH->proj.synth.bpm);
}

/* ---- update loop ---------------------------------------------------------- */
static int update(void *ud)
{
    (void)ud;
    input_frame();
    evo_frame();                          /* CORROSION: prepare the next mutation */
    app_fill_frame();
    screen_frame(fill_on);
    return 1;
}

/* ---- CPU TEST (milestone 1 only) --------------------------------------------
 * ETCH DS's CPU test: ACID retriggers with an accent on every 16th, all six
 * drum lanes fire on every 16th (CMD_STRESS), and every FX stage works.  A
 * temporary system-menu checkbox so the meter can be read on hardware; the
 * menu's three items belong to CODE, DEMOS and SPEAKER/TILT later. */
static fx_params_t fx_saved;
static void cpu_test_cb(void *ud)
{
    PDMenuItem *item = (PDMenuItem *)ud;
    int on = pd->system->getMenuItemValue(item);
    if (on) {
        fx_saved = fx_p;
        fx_p.drive = 50; fx_p.crush_bits = 10; fx_p.crush_hold = 3;
        fx_p.dly_mix = 60; fx_p.dly_fb = 80; fx_p.comb_mix = 40; fx_p.comb_fb = 90;
    } else {
        fx_p = fx_saved;
    }
    app_send(CMD(CMD_STRESS, on ? 1 : 0));
    screen_message(on ? "CPU TEST: ACID + 6 LANES EVERY 16TH, ALL FX" : "CPU TEST OFF");
}

/* ---- boot -------------------------------------------------------------------- */
static void boot(void)
{
    uint16_t e;
    pd->display->setRefreshRate(50);
    pd->system->setCrankSoundsDisabled(1);
    clk_init();
    audio_engine_init();
    foundry_build();                      /* FOUNDRY renders its drum sounds */
    shared_init();
    pump_init(SH);
    crank_ui_init(acid_p.cutoff, acid_p.res, (uint8_t)pd->system->isCrankDocked());
    screen_init(pd);

    /* launch it, hear music: CORRODE plays in DRIFT at once */
    e = hw_noise();
    ui_entropy = e;
    evo_seed(e);
    app_send(CMD(CMD_START, e));
    pd->sound->addSource(audio_cb, NULL, 1);
    {
        PDMenuItem *it = pd->system->addCheckmarkMenuItem("cpu test", 0, cpu_test_cb, NULL);
        pd->system->setMenuItemUserdata(it, it);
    }
    pd->system->setUpdateCallback(update, NULL);
    pd->system->logToConsole("ETCH CRANK %s -- by Synth Centric -- for Playdate", ETCH_CRANK_VERSION);
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg)
{
    (void)arg;
    if (event == kEventInit) {
        pd = playdate;
        boot();
    } else if (event == kEventPause) {
        menu_open = 1;
        cb_paused = 0;
        screen_menu(1);
    } else if (event == kEventResume) {
        menu_open = 0;
        pd->system->logToConsole("ETCH CRANK: %u audio callbacks ran while the system menu was open",
                                 (unsigned)cb_paused);
        screen_menu(0);
    }
    return 0;
}

/* screen.c reads these */
uint32_t main_menu_callbacks(void) { return cb_paused; }
