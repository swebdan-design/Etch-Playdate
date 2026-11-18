/*
 * ETCH CRANK -- by Synth Centric
 * What the screens need from the platform (main.c on the device and the
 * Simulator; the host tests provide their own).
 */
#ifndef ETCH_CRANK_PLAT_H
#define ETCH_CRANK_PLAT_H
#include <stdint.h>
double plat_tick_hz(void);      /* the CPU meter's clock rate (measured)        */
double plat_ui_ms(void);        /* the update loop's last frame, milliseconds   */
void   synth_frame(void);       /* scr_synth.c: deferred drum re-synthesis      */
/* foundry_guard.c: ETCH DS's foundry_rebuild with the triggers held back */
void   foundry_guard_rebuild(uint32_t lane, uint32_t kit);
void   foundry_guard_flush(void);   /* the pump, once per block */
#endif
