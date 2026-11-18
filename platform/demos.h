/*
 * ETCH CRANK -- by Synth Centric
 * ETCH's eight ROM tracks (FILE -> LOAD -> D1..D8), byte for byte.
 */
#ifndef ETCH_CRANK_DEMOS_H
#define ETCH_CRANK_DEMOS_H
#include <stddef.h>
#include "etch.h"
#define DEMO_COUNT 8
extern const char *const demo_names[DEMO_COUNT];
void demos_load(uint8_t t, project_t *p);
#endif
