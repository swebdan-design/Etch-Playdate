/*
 * ETCH DS -- by Synth Centric
 * Project storage on the SD card: one file of fixed slots, ETCH's scheme.
 *
 * /etch_ds/etch_ds.sav = 16 slots of 16 KB + a 16-byte metadata block,
 * mirroring ETCH's SRAM layout (8 slots of 4 KB + metadata).  Each slot:
 *     "ETDS" | version | 0 | 0 0 | size (LE32) | Fletcher-16 (LE16) | 0 0 | data
 * data = ETCH's project_t, byte for byte, followed by the DS-only settings
 * (ds_ext_t).  A save invalidates the magic, writes the data, computes the
 * checksum from what is actually on the card, writes the header and finally
 * the magic, then reads everything back.  Each phase closes the file so the
 * card sees them in that order.  Empty, foreign or corrupt slots are reported
 * and never loaded.
 *
 * ETCH Game Boy saves (a 32 KB .sav of the cartridge SRAM) can be read and
 * imported, read only: /etch_ds/etch_gb.sav.
 */
#ifndef ETCH_DS_STORAGE_H
#define ETCH_DS_STORAGE_H
#include <stdint.h>
#include "etch.h"
#include "audio.h"

#define DS_SLOTS        16
#define DS_SLOT_SIZE    16384u
#define DS_SAVE_VERSION 2              /* v1 slots (no drum designs) still load */
#define GB_SLOTS        8

#define SLOT_EMPTY 0
#define SLOT_OK    1
#define SLOT_BAD   2
#define SAVE_NONE  0xFF

typedef struct {
    acid_params_t acid;
    fx_params_t   fx;
    uint8_t       drum_tune[DS_LANES], drum_decay[DS_LANES], drum_level[DS_LANES];
    uint8_t       has_drums;          /* set by storage_load: v2 slot        */
    uint8_t       reserved[13];
    /* ---- save version 2 ---- */
    drum_def_t    drums[NUM_LANES][KITS_PER_LANE];   /* edited drum sounds  */
} ds_ext_t;

/* paths (the host tests point these elsewhere) */
extern const char *storage_path, *storage_gb_path;

uint16_t etch_fletcher16(const uint8_t *p, uint32_t n);   /* ETCH's checksum */
uint8_t storage_init(void);                                 /* 1 = card ready */
uint8_t storage_slot_status(uint8_t slot, char *name10);
uint8_t storage_save(uint8_t slot, const project_t *p, const ds_ext_t *x);   /* 1 = verified */
uint8_t storage_load(uint8_t slot, project_t *p, ds_ext_t *x);
uint8_t storage_erase(uint8_t slot);
uint8_t storage_last_slot(void);
uint8_t storage_gb_status(uint8_t slot, char *name10);
uint8_t storage_gb_load(uint8_t slot, project_t *p);
#endif
