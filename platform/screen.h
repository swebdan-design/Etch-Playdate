/*
 * ETCH CRANK -- by Synth Centric
 * Milestone 1's screen: the status band (cutoff arc around the scope, mode,
 * bars to the next evolution, generation, song position), LIVE's pattern
 * view with the dithered fade of evolved steps, and the CPU meter.
 * Only what changed is redrawn.
 */
#ifndef ETCH_CRANK_SCREEN_H
#define ETCH_CRANK_SCREEN_H
#include <stdint.h>
#include "pd_api.h"

void screen_init(PlaydateAPI *pd);
void screen_frame(uint8_t fill_on);
void screen_message(const char *msg);
void screen_menu(uint8_t open);

#endif
