/*
 * ETCH CRANK -- by Synth Centric
 * Projects (see proj.c).
 */
#ifndef ETCH_CRANK_PROJ_H
#define ETCH_CRANK_PROJ_H
#include <stdint.h>
void    proj_init(void);
void    proj_new(void);
void    proj_demo(uint8_t d);
void    proj_boot_song(void);
uint8_t proj_save(uint8_t slot);
uint8_t proj_load(uint8_t slot);
uint8_t proj_erase(uint8_t slot);
uint8_t proj_import_ds(uint8_t slot);
uint8_t proj_import_gb(uint8_t slot);
/* which: 0 own saves, 1 ETCH DS saves, 2 ETCH Game Boy saves */
uint8_t proj_slot_status(uint8_t which, uint8_t slot, char *name10);
#endif
