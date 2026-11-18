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
 *   update loop     buttons and crank (ui.c), the Playdate-only layer (cpl.c),
 *                   CORROSION's background work (evolve.c), the screen (only
 *                   rows that changed reach the display).
 */
#include <stdio.h>
#include <string.h>
#include "pd_api.h"
#include "app_glue.h"
#include "audio.h"
#include "corrosion.h"
#include "pump.h"
#include "crank.h"
#include "ui.h"
#include "gfx1.h"
#include "cpl.h"
#include "proj.h"
#include "plat.h"
#include "layers.h"

static PlaydateAPI *pd;
void pdfile_init(PlaydateAPI *api);

/* ---- the clock for the CPU meter ------------------------------------------
 * Device: the Cortex-M7's cycle counter (DWT CYCCNT), raw CPU cycles.
 * Simulator: the host's monotonic clock in nanoseconds. */
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

/* the clock rate, measured against the millisecond clock every 2 s */
static double tick_hz = 168e6, ui_ms;
static uint32_t cal_t0, cal_ms0;
static void calibrate(uint32_t ms)
{
    uint32_t t = host_clock ? host_clock() : 0;
    if (!cal_ms0) { cal_ms0 = ms; cal_t0 = t; return; }
    if (ms - cal_ms0 >= 2000) {
        double hz = (double)(uint32_t)(t - cal_t0) * 1000.0 / (double)(ms - cal_ms0);
        if (hz > 1e6) tick_hz = hz;
        cal_ms0 = ms;
        cal_t0 = t;
    }
}
double plat_tick_hz(void) { return tick_hz; }
double plat_ui_ms(void) { return ui_ms; }

/* ---- the audio callback ----------------------------------------------------- */
static int audio_cb(void *ctx, int16_t *left, int16_t *right, int len)
{
    uint32_t t0 = host_clock ? host_clock() : 0, dt;
    (void)ctx;
    pump_pull(left, right, (uint32_t)len);
    dt = (host_clock ? host_clock() : 0) - t0;
    pstats.cb_sum += dt;
    if (dt > pstats.cb_max) pstats.cb_max = dt;
    return 1;
}

/* the mic level, for the mod matrix (only while a row uses it) */
static int mic_cb(void *ctx, int16_t *buf, int len)
{
    int i, pk = 0;
    (void)ctx;
    for (i = 0; i < len; i++) { int v = buf[i] < 0 ? -buf[i] : buf[i]; if (v > pk) pk = v; }
    pk >>= 8;
    cpl_mic_level = (uint8_t)(pk > 127 ? 127 : pk);
    return 1;
}

/* ---- the system menu: SPEAKER, TILT, CRANK BITE ------------------------------ */
static PDMenuItem *mi_speaker, *mi_tilt, *mi_bite;
static void menu_cb(void *ud)
{
    (void)ud;
    cpl_set.speaker = (uint8_t)pd->system->getMenuItemValue(mi_speaker);
    cpl_set.tilt = (uint8_t)pd->system->getMenuItemValue(mi_tilt);
    cpl_set.bite = (uint8_t)pd->system->getMenuItemValue(mi_bite);
    cpl_settings_save();
}

static int headphones = 1;
static void hp_changed(int hp, int mic) { (void)mic; headphones = hp; }

/* ---- present: the rows that changed, into the display frame ------------------- */
static void present(void)
{
    uint8_t *fr = pd->graphics->getFrame();
    int y, first = -1, last = -1;
    for (y = 0; y < GFX_H; y++) {
        if (!gfx_dirty[y]) continue;
        memcpy(fr + y * LCD_ROWSIZE, gfx_fb + y * GFX_STRIDE, GFX_W / 8);
        gfx_dirty[y] = 0;
        if (first < 0) first = y;
        last = y;
    }
    if (first >= 0) pd->graphics->markUpdatedRows(first, last);
}

/* ---- the update loop ---------------------------------------------------------- */
static uint8_t mic_on, accel_on;
static int update(void *ud)
{
    PDButtons cur, pushed, released;
    ui_in_t in;
    float ax = 0, ay = 0, az = 0;
    uint32_t t0 = host_clock ? host_clock() : 0;
    (void)ud;

    pd->system->getButtonState(&cur, &pushed, &released);
    in.held = (uint8_t)cur;
    in.pushed = (uint8_t)pushed;
    in.released = (uint8_t)released;
    in.crank_q8 = (int32_t)(pd->system->getCrankChange() * 256.0f);
    in.docked = (uint8_t)pd->system->isCrankDocked();
    in.now_ms = pd->system->getCurrentTimeMilliseconds();
    calibrate(in.now_ms);

    if (cpl_tilt_wanted != accel_on) {
        accel_on = cpl_tilt_wanted;
        pd->system->setPeripheralsEnabled(accel_on ? kAccelerometer : kNone);
    }
    if (accel_on) pd->system->getAccelerometer(&ax, &ay, &az);
    if (cpl_mic_wanted != mic_on) {
        mic_on = cpl_mic_wanted;
        pd->sound->setMicCallback(mic_on ? mic_cb : NULL, NULL, kMicInputAutodetect);
        if (!mic_on) cpl_mic_level = 0;
    }

    screen_frame(&in);                          /* buttons, crank, the screen */
    cpl_frame(in.now_ms, ax, headphones);   /* lanes, mods, TILT, SPEAKER */
    layers_frame();                         /* the DS layers */
    synth_frame();                          /* deferred drum re-synthesis */
    evo_frame();                            /* CORROSION: the next mutation */
    app_fill_frame();
    present();
    ui_ms = (double)(uint32_t)((host_clock ? host_clock() : 0) - t0) * 1000.0 / tick_hz;
    return 1;
}

/* ---- boot -------------------------------------------------------------------- */
static void boot(void)
{
    static const char *spk[3] = { "auto", "on", "off" };
    uint16_t e;
    int hp = 1, mic = 0;
    pd->display->setRefreshRate(50);
    pd->system->setCrankSoundsDisabled(1);
    pdfile_init(pd);
    clk_init();
    audio_engine_init();
    foundry_build();                      /* FOUNDRY renders its drum sounds */
    shared_init();
    proj_init();
    cpl_init();
    cpl_settings_load();
    pump_init(SH);
    crank_ui_init(acid_p.cutoff, acid_p.res, (uint8_t)pd->system->isCrankDocked());
    screen_init();

    mi_speaker = pd->system->addOptionsMenuItem("speaker", spk, 3, menu_cb, NULL);
    pd->system->setMenuItemValue(mi_speaker, cpl_set.speaker);
    mi_tilt = pd->system->addCheckmarkMenuItem("tilt", cpl_set.tilt, menu_cb, NULL);
    mi_bite = pd->system->addCheckmarkMenuItem("crank bite", cpl_set.bite, menu_cb, NULL);
    pd->sound->getHeadphoneState(&hp, &mic, hp_changed);
    headphones = hp;

    /* launch it, hear music: CORRODE plays in DRIFT at once */
    e = hw_noise();
    ui_entropy = e;
    evo_seed(e);
    app_send(CMD(CMD_START, e));
    pd->sound->addSource(audio_cb, NULL, 1);
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
    } else if (event == kEventResume) {
        cal_ms0 = 0;                      /* the game's clock stood still */
    } else if (event == kEventTerminate) {
        cpl_settings_save();
    }
    return 0;
}
